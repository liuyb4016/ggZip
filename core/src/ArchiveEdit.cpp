#include "SevenZipEngine.h"
#include "FileTransaction.h"
#include "ggzip/ArchivePath.h"
#include "ggzip/Diagnostics.h"
#include "ggzip/ArchiveVolumes.h"
#include "ggzip/Format.h"
#include "ggzip/OperationError.h"
#include "ggzip/SourceMark.h"
#include "ggzip/Text.h"
#include <algorithm>
#include <cwctype>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace ggzip {
namespace {

std::wstring memberName(std::wstring path) {
    if (!path.empty() && (path.back() == L'/' || path.back() == L'\\')) path.pop_back();
    return validatedPreviewPath(path);
}

std::wstring folded(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t character) { return static_cast<wchar_t>(std::towupper(character)); });
    return value;
}

bool inside(const std::wstring& member, const std::wstring& parent) {
    return member == parent || member.rfind(parent + L"/", 0) == 0;
}

}

OperationSummary SevenZipEngine::edit(const std::wstring& archive, const EditOptions& opt,
                                     const ProgressFn& onProgress, const std::atomic_bool& cancel) const {
    ensure();
    const fs::path target = fs::absolute(archive).lexically_normal();
    const auto type = archiveTypeFromExt(target.wstring());
    if (type != L"zip" && type != L"7z") throw ArchiveError(diagnostic("仅支持编辑单文件 ZIP 或 7z 压缩包"));
    files::safeParents(target.parent_path());
    const auto expected = files::identity(target);
    if (!expected.exists || (expected.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        throw ArchiveError(diagnostic("编辑目标不是普通压缩包"));
    if (archiveVolumes(target).split || files::family(target).size() != 1)
        throw ArchiveError(diagnostic("分卷压缩包仅支持读取，不能编辑"));
    OperationSummary summary;
    summary.destination = target.wstring();
    if (cancel.load()) { summary.outcome = Outcome::Cancelled; return summary; }
    files::Workspace workspace(target.parent_path());
    const fs::path staged = workspace.snapshot(target, target.filename(), cancel);
    std::vector<Entry> original;
    const auto information = inspect(staged.wstring(), &original, {opt.password, true, &cancel});
    if (information.typeName != "zip" && information.typeName != "7z")
        throw ArchiveError(diagnostic("仅支持编辑 ZIP 或 7z 压缩包内容"));
    std::map<std::wstring, Entry> expectedMembers;
    std::set<std::wstring> removals;
    for (const auto& path : opt.remove) removals.insert(memberName(path));
    std::vector<MemberRename> renames;
    for (const auto& item : opt.rename) {
        const auto source = memberName(item.source);
        const auto destination = memberName(item.target);
        if (inside(destination, source) || inside(source, destination))
            throw ConflictError(diagnostic("不能将条目重命名到自身或自身子目录"));
        renames.push_back({source, destination});
    }
    for (std::size_t first = 0; first < renames.size(); ++first) {
        for (std::size_t second = first + 1; second < renames.size(); ++second)
            if (inside(renames[first].source, renames[second].source) ||
                inside(renames[second].source, renames[first].source))
                throw ConflictError(diagnostic("重命名范围不能重叠"));
    }
    std::set<std::wstring> foundRemove;
    std::set<std::wstring> foundRename;
    std::set<std::wstring> occupied;
    for (const auto& entry : original) {
        const auto name = memberName(entry.path);
        if (entry.isLink) throw ArchiveError(diagnostic("含链接的压缩包不能安全编辑"));
        bool removed = false;
        for (const auto& removal : removals) {
            if (inside(name, removal)) { removed = true; foundRemove.insert(removal); }
        }
        if (removed) continue;
        auto changedName = name;
        for (const auto& rename : renames) {
            if (inside(name, rename.source)) {
                changedName = rename.target + name.substr(rename.source.size());
                foundRename.insert(rename.source);
            }
        }
        if (!occupied.insert(folded(changedName)).second) throw ConflictError(diagnostic("编辑后存在重复目标"));
        expectedMembers.emplace(changedName, entry);
    }
    if (foundRemove.size() != removals.size() || foundRename.size() != renames.size())
        throw ArchiveError(diagnostic("要删除或重命名的条目不存在"));
    auto arguments = [&](const std::wstring& command) {
        std::vector<std::wstring> values{command, L"-spd", L"-ssc", L"-y"};
        if (!opt.password.empty()) values.push_back(L"-p" + opt.password);
        return values;
    };
    files::Resolver resolver(opt.conflict, opt.resolveConflict);
    std::map<std::wstring, fs::path> addedFiles;
    std::vector<MemberAddition> additions;
    const auto workspaceState = files::identity(workspace.root);
    auto objectIs = [](const files::Identity& first, const files::Identity& second) {
        return first.exists && second.exists && first.info.dwVolumeSerialNumber == second.info.dwVolumeSerialNumber &&
            first.info.nFileIndexHigh == second.info.nFileIndexHigh && first.info.nFileIndexLow == second.info.nFileIndexLow;
    };
    for (const auto& input : opt.add) {
        const fs::path source = fs::absolute(input.source);
        const auto state = files::identity(source);
        if (objectIs(state, expected)) throw ArchiveError(diagnostic("不能将压缩包添加到自身"));
        additions.push_back({source.wstring(), memberName(input.target)});
        if (state.exists && (state.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            files::safeParents(source);
            for (fs::recursive_directory_iterator iterator(source), end; iterator != end; ++iterator) {
                if (cancel.load()) { summary.outcome = Outcome::Cancelled; return summary; }
                const auto itemState = files::identity(iterator->path());
                if (objectIs(itemState, workspaceState)) { iterator.disable_recursion_pending(); continue; }
                if (objectIs(itemState, expected)) { ++summary.skipped; continue; }
                additions.push_back({iterator->path().wstring(), memberName(input.target) + L"/" +
                    iterator->path().lexically_relative(source).generic_wstring()});
            }
        }
    }
    std::map<std::wstring, std::wstring> directoryRenames;
    std::set<std::wstring> skippedDirectories;
    try {
        if (!removals.empty()) {
            auto values = arguments(L"d");
            for (const auto& removal : removals) {
                for (const auto& entry : original) {
                    const auto name = memberName(entry.path);
                    if (inside(name, removal)) values.push_back(L"-i!" + name);
                }
            }
            values.push_back(L"--");
            values.push_back(staged.wstring());
            runWithProgress(values, onProgress, cancel, diagnostic("删除条目"));
            if (!cancel.load()) workspace.adoptOutput(staged);
        }
        for (const auto& rename : renames) {
            auto values = arguments(L"rn");
            values.insert(values.end(), {L"--", staged.wstring(), rename.source, rename.target});
            runWithProgress(values, onProgress, cancel, diagnostic("重命名条目"));
            if (!cancel.load()) workspace.adoptOutput(staged);
        }
        const auto additionsRoot = workspace.root / L"additions";
        std::string includeList;
        for (const auto& addition : additions) {
            const auto requested = memberName(addition.target);
            if (std::any_of(skippedDirectories.begin(), skippedDirectories.end(),
                [&](const auto& directory) { return inside(requested, directory); })) { ++summary.skipped; continue; }
            auto name = requested;
            for (auto iterator = directoryRenames.rbegin(); iterator != directoryRenames.rend(); ++iterator) {
                if (inside(requested, iterator->first)) {
                    name = iterator->second + requested.substr(iterator->first.size());
                    break;
                }
            }
            const auto sourceState = files::identity(fs::path(addition.source));
            const bool directory = sourceState.exists && (sourceState.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
            const auto collision = std::find_if(expectedMembers.begin(), expectedMembers.end(),
                [&](const auto& item) { return folded(item.first) == folded(name); });
            const bool merge = collision != expectedMembers.end() && directory && collision->second.isDir;
            if (merge) {
                name = collision->first;
                directoryRenames[requested] = name;
            }
            if (collision != expectedMembers.end() && !merge) {
                const auto decision = resolver.choose({addition.source, name, directory || collision->second.isDir});
                if (cancel.load()) break;
                if (decision == ConflictPolicy::Skip) {
                    if (directory) skippedDirectories.insert(requested);
                    ++summary.skipped;
                    continue;
                }
                if (decision == ConflictPolicy::Rename) {
                    name = files::renamed(fs::path(name), [&](const fs::path& candidate) {
                        return occupied.count(folded(candidate.generic_wstring())) != 0;
                    }).generic_wstring();
                    if (directory) directoryRenames[requested] = name;
                    ++summary.renamed;
                } else if (decision == ConflictPolicy::Overwrite && !directory && !collision->second.isDir) {
                    occupied.erase(folded(collision->first));
                    expectedMembers.erase(collision);
                    ++summary.overwritten;
                } else throw ConflictError(diagnostic("添加条目与已有条目冲突"));
            }
            if (!merge && !occupied.insert(folded(name)).second) throw ConflictError(diagnostic("添加条目目标重复"));
            if (directory) {
                const auto path = additionsRoot / fs::path(name);
                files::makeDirectories(path);
                workspace.adopt(path);
                Entry entry;
                entry.path = name + L"/";
                entry.isDir = true;
                expectedMembers[name] = entry;
            } else {
                const auto copied = workspace.snapshot(fs::absolute(addition.source), fs::path(L"additions") / fs::path(name), cancel);
                addedFiles[name] = copied;
            }
            includeList += utf8(name) + "\n";
        }
        if (!includeList.empty() && !cancel.load()) {
            const auto listFile = workspace.write(L"additions.lst", includeList);
            auto values = arguments(L"a");
            values.push_back(L"-scsUTF-8");
            values.push_back(L"-i@" + listFile.wstring());
            if (type == L"7z" && !opt.password.empty()) values.push_back(L"-mhe=on");
            values.insert(values.end(), {L"--", staged.wstring()});
            runWithProgress(values, onProgress, cancel, diagnostic("添加条目"), 0, {}, additionsRoot.wstring());
            if (!cancel.load()) workspace.adoptOutput(staged);
        }
        if (cancel.load()) { summary.outcome = Outcome::Cancelled; return summary; }
        const auto modified = list(staged.wstring(), {opt.password, true, &cancel});
        std::map<std::wstring, Entry> actual;
        std::set<std::wstring> allNames;
        for (const auto& entry : modified) {
            const auto name = memberName(entry.path);
            if (entry.isLink || !allNames.insert(folded(name)).second)
                throw ArchiveError(diagnostic("编辑结果包含不安全或重复条目"));
            actual[name] = entry;
        }
        for (const auto& item : expectedMembers) {
            const auto found = actual.find(item.first);
            if (found == actual.end() || found->second.isDir != item.second.isDir ||
                (!item.second.isDir && (!item.second.crcKnown || !found->second.crcKnown ||
                    found->second.size != item.second.size || found->second.crc32 != item.second.crc32)))
                throw ArchiveError(diagnostic("编辑后未操作条目的校验信息发生变化"));
        }
        for (const auto& item : addedFiles) {
            const auto found = actual.find(item.first);
            if (found == actual.end() || found->second.isDir ||
                !files::verified(item.second, found->second, true, &cancel))
                throw ArchiveError(diagnostic("新增条目未通过完整内容校验"));
        }
        for (const auto& entry : modified) {
            if (entry.isDir) continue;
            const auto name = memberName(entry.path);
            if (!expectedMembers.count(name) && !addedFiles.count(name))
                throw ArchiveError(diagnostic("编辑结果包含未请求的文件"));
            for (auto separator = name.find(L'/'); separator != std::wstring::npos;
                 separator = name.find(L'/', separator + 1)) {
                const auto parent = actual.find(name.substr(0, separator));
                if (parent != actual.end() && !parent->second.isDir)
                    throw ArchiveError(diagnostic("编辑后文件被用作父目录"));
            }
        }
        test(staged.wstring(), opt.password, onProgress, cancel);
        if (cancel.load()) { summary.outcome = Outcome::Cancelled; return summary; }
        applySourceZone(staged.wstring(), sourceZone(target.wstring()));
        files::commitFamily(workspace, {{staged, target}}, {target}, {expected}, {}, &cancel);
    } catch (const OperationError&) { throw; }
    catch (...) {
        if (cancel.load() && !workspace.preserve) { summary.outcome = Outcome::Cancelled; return summary; }
        throw;
    }
    summary.paths.push_back(target.wstring());
    summary.completed = 1;
    return summary;
}

}

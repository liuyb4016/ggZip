#include "SevenZipEngine.h"
#include "ProcessUtil.h"
#include "FileTransaction.h"
#include "ggzip/OperationError.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/Diagnostics.h"
#include "ggzip/Format.h"
#include "ggzip/ArchivePath.h"
#include "ggzip/ArchiveVolumes.h"
#include "ggzip/SourceMark.h"
#include "ggzip/PasswordError.h"
#include "ggzip/Text.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdlib>
#include <map>
#include <set>
#include <filesystem>
#include <limits>

namespace fs = std::filesystem;
namespace ggzip {
namespace {

bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void requireFile(const std::wstring& p, const char* what) {
    if (!fileExists(p)) throw ArchiveError(std::string(what) + diagnostic(": 文件不存在 - ") + utf8(p));
}

bool passwordFailure(std::string message) {
    std::transform(message.begin(), message.end(), message.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return message.find("wrong password") != std::string::npos ||
           message.find("enter password") != std::string::npos ||
           message.find("password is incorrect") != std::string::npos ||
           message.find("can not open encrypted archive") != std::string::npos;
}

// 从 -bsp1 进度流缓冲里解析最近的 "NN%"；解析不到返回 -1。
int parsePercent(const std::string& buf) {
    for (size_t i = buf.size(); i > 0; --i) {
        if (buf[i - 1] == '%') {
            size_t minB = (i >= 4) ? i - 4 : 0;
            size_t b = i - 1;
            while (b > minB && std::isdigit((unsigned char)buf[b - 1])) --b;
            if (b < i - 1) {
                int v = std::atoi(buf.c_str() + b);
                if (v >= 0 && v <= 100) return v;
            }
            return -1;
        }
    }
    return -1;
}

struct Listing {
    ArchiveInfo info;
    std::vector<Entry> entries;
};

// 解析 `7z l -slt -sccUTF-8` 输出。
// 每个块都从 "Path = " 行开始；含 "Size" 字段的是条目块，
// 否则是压缩包自身的元数据块（Type / Physical Size）。
Listing parseListing(const std::string& text, bool strict = false) {
    Listing out;
    std::map<std::string, std::string> cur;
    bool readingEntries = false;

    auto flush = [&]() {
        if (cur.empty()) return;
        std::map<std::string, std::string> fields = std::move(cur);
        cur.clear();

        auto num = [&fields](const char* key) -> std::uint64_t {
            auto it = fields.find(key);
            return it == fields.end() ? 0 : std::strtoull(it->second.c_str(), nullptr, 10);
        };
        auto str = [&fields](const char* key) -> std::string {
            auto it = fields.find(key);
            return it == fields.end() ? std::string() : it->second;
        };

        if (!readingEntries) {
            // 元数据块
            if (!str("Type").empty()) out.info.typeName = str("Type");
            out.info.physicalSize = std::max(out.info.physicalSize,
                std::max(num("Physical Size"), num("Total Physical Size")));
            return;
        }
        if (!fields.count("Size")) {
            if (strict) throw ArchiveError(diagnostic("条目信息缺少大小字段"));
            return;
        }

        auto itPath = fields.find("Path");
        if (itPath == fields.end() || itPath->second.empty()) return;

        Entry e;
        std::wstring raw = wideFromUtf8(itPath->second);
        std::replace(raw.begin(), raw.end(), L'\\', L'/');
        e.path = raw;
        e.size = num("Size");
        e.sizeKnown = !str("Size").empty();
        e.packedSize = num("Packed Size");

        e.isDir = (str("Folder") == "+");
        if (!e.isDir) {
            auto itAttr = fields.find("Attributes");
            if (itAttr != fields.end() && !itAttr->second.empty())
                e.isDir = (itAttr->second[0] == 'D');
        }
        if (!e.path.empty() && e.path.back() == L'/') e.isDir = true;
        if (e.isDir && !e.path.empty() && e.path.back() != L'/') e.path += L'/';

        e.encrypted = (str("Encrypted") == "+");
        e.crc32 = (std::uint32_t)std::strtoul(str("CRC").c_str(), nullptr, 16);
        e.crcKnown = !str("CRC").empty();
        e.modified = str("Modified");
        e.method = str("Method");
        const std::string attributes = str("Attributes");
        const std::string mode = str("Mode");
        e.isLink = !str("Symbolic Link").empty() || !str("Hard Link").empty() ||
                   attributes.find_first_of("Ll") != std::string::npos ||
                   (!mode.empty() && mode.front() == 'l');

        out.entries.push_back(std::move(e));
    };

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        std::string_view line(text);
        if (eol == std::string::npos) {
            line = line.substr(pos);
            pos = text.size() + 1;
        } else {
            line = line.substr(pos, eol - pos);
            pos = eol + 1;
        }
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        if (line == "----------") {
            if (readingEntries) throw ArchiveError(diagnostic("压缩包包含不明确的条目信息边界"));
            flush();
            readingEntries = true;
            continue;
        }

        if (line.rfind("Path = ", 0) == 0) {
            flush();
            cur["Path"] = std::string(line.substr(7));
            continue;
        }
        auto eq = line.find(" = ");
        if (eq != std::string_view::npos) {
            const std::string key(line.substr(0, eq));
            if (strict && cur.count(key))
                throw ArchiveError(diagnostic("预览条目信息包含重复字段，不能安全解析"));
            cur[key] = std::string(line.substr(eq + 3));
        }
    }
    flush();
    out.info.format = formatFromTypeName(out.info.typeName);
    return out;
}

} // namespace

bool SevenZipEngine::available() const { return !exe_.empty(); }

std::wstring SevenZipEngine::enginePath() const { return exe_; }

std::wstring SevenZipEngine::resolveFirstVolume(const std::wstring& path) const {
    return archiveVolumes(path).first.wstring();
}

void SevenZipEngine::ensure() const {
    if (exe_.empty())
        throw ArchiveError(std::string(diagnostic("内置 7-Zip 引擎文件缺失。请重新安装或完整解压 ggZip，保留 7z 子目录。")) +
                    diagnostic("源码开发时可设置 GGZIP_7Z 指向自备的 7z.exe。"));
}

ArchiveInfo SevenZipEngine::inspect(const std::wstring& archive,
                                    std::vector<Entry>* entries, const InspectOptions& opt) const {
    ensure();
    const std::wstring real = resolveFirstVolume(archive);
    requireFile(real, diagnostic("压缩包"));
    std::string text;
    static std::atomic_bool noCancel{ false };
    std::vector<std::wstring> arguments{ L"l", L"-slt", L"-sccUTF-8" };
    if (!opt.password.empty()) arguments.push_back(L"-p" + opt.password);
    arguments.push_back(L"--");
    arguments.push_back(real);
    proc::RunResult r = proc::run(exe_, arguments,
        [&](std::string_view chunk) {
            if (chunk.size() > opt.maxListingBytes - text.size()) throw ArchiveError(diagnostic("条目列表超过资源限制"));
            text.append(chunk);
            return true;
        }, opt.cancel ? *opt.cancel : noCancel, {opt.deadlineTick});
    if (opt.cancel && opt.cancel->load()) throw ArchiveError(diagnostic("读取压缩包已取消"));
    if (r.exitCode != 0) {
        std::string msg = diagnostic("读取压缩包失败 (exit ") + std::to_string(r.exitCode) + "): " + utf8(archive);
        if (!r.stderrTail.empty()) msg += "\n" + r.stderrTail;
        if (passwordFailure(r.stderrTail) || passwordFailure(text)) throw PasswordError(msg);
        throw ArchiveError(msg);
    }
    Listing lst = parseListing(text, opt.strict);
    if (entries) *entries = std::move(lst.entries);
    return lst.info;
}

std::vector<Entry> SevenZipEngine::list(const std::wstring& archive, const InspectOptions& opt) const {
    std::vector<Entry> es;
    inspect(archive, &es, opt);
    return es;
}

void SevenZipEngine::runWithProgress(std::vector<std::wstring> args,
                                     const ProgressFn& onProgress,
                                     const std::atomic_bool& cancel,
                                     const std::string& what, std::uint64_t deadline,
                                     const std::function<void()>& monitor, const std::wstring& workingDirectory) const {
    const auto endOfOptions = std::find(args.begin(), args.end(), L"--");
    args.insert(endOfOptions, {L"-sccUTF-8", L"-bsp1"});
    std::string buf;
    double last = -1;
    proc::RunOptions limits;
    limits.deadlineTick = deadline;
    limits.monitor = monitor;
    limits.workingDirectory = workingDirectory;
    proc::RunResult r = proc::run(exe_, args, [&](std::string_view c) {
        buf.append(c);
        if (buf.size() > 8192) buf.erase(0, buf.size() - 8192);
        if (onProgress) {
            int p = parsePercent(buf);
            if (p >= 0 && p != last) {
                last = p;
                onProgress({ (double)p, {} });
            }
        }
        return true;
    }, cancel, limits);
    if (cancel.load()) return;   // 用户取消：由调用方处理，不作为错误
    if (r.exitCode != 0) {
        std::string msg = what + diagnostic(" 失败 (exit ") + std::to_string(r.exitCode) + ")";
        if (!r.stderrTail.empty()) {
            std::string t = r.stderrTail.size() > 2048
                          ? r.stderrTail.substr(r.stderrTail.size() - 2048) : r.stderrTail;
            msg += "\n" + t;
        }
        if (passwordFailure(r.stderrTail) || passwordFailure(buf)) throw PasswordError(msg);
        throw ArchiveError(msg);
    }
}

OperationSummary SevenZipEngine::extract(const std::wstring& archive, const std::wstring& outDir,
                             const std::vector<std::wstring>& include,
                             const ExtractOptions& opt, const ProgressFn& onProgress,
                             const std::atomic_bool& cancel) const {
    ensure();
    OperationSummary summary;
    const fs::path destination = fs::absolute(outDir).lexically_normal();
    summary.destination = destination.wstring();
    files::safeParents(destination);
    files::Workspace workspace(destination.parent_path());
    const auto volumes = archiveVolumes(archive);
    const fs::path source = volumes.first;
    requireFile(source.wstring(), diagnostic("压缩包"));
    unsigned zone = 0;
    for (const auto& part : volumes.parts) zone = std::max(zone, sourceZone(part.wstring()));
    if (zone && !fileSystemCapabilities(destination).namedStreams)
        throw ArchiveError(diagnostic("目标文件系统不支持下载来源标记，文件未发布"));
    fs::path base = source;
    const std::wstring suffix = source.extension().wstring();
    if (suffix.size() >= 4 && std::all_of(suffix.begin() + 1, suffix.end(),
            [](wchar_t character) { return character >= L'0' && character <= L'9'; }))
        base.replace_extension();
    const bool tarWrapped = isTarWrappedExt(base.wstring());
    fs::path snapshot;
    try {
        const auto& sources = volumes.parts;
        std::vector<files::Identity> sourceStates;
        std::uint64_t needed = 16ull * 1024 * 1024;
        for (const auto& part : sources) {
            sourceStates.push_back(files::identity(part));
            const auto size = fs::file_size(part);
            if (size > std::numeric_limits<std::uint64_t>::max() - needed)
                throw ArchiveError(diagnostic("输入大小超出支持范围"));
            needed += size;
        }
        if (fs::space(workspace.root).available < needed) throw ArchiveError(diagnostic("磁盘空间不足，无法安全创建输入快照"));
        for (const auto& part : sources) {
            const auto copied = workspace.snapshot(part, fs::path(L"source") / part.filename(), cancel);
            if (_wcsicmp(part.wstring().c_str(), source.wstring().c_str()) == 0) snapshot = copied;
        }
        for (std::size_t index = 0; index < sources.size(); ++index)
            if (!files::same(files::identity(sources[index]), sourceStates[index]))
                throw ArchiveError(diagnostic("创建快照期间分卷发生变化，操作未继续"));
    } catch (...) {
        if (cancel.load()) { summary.outcome = Outcome::Cancelled; return summary; }
        throw;
    }
    if (snapshot.empty()) throw ArchiveError(diagnostic("没有可用的输入快照"));
    std::vector<Entry> entries = list(snapshot.wstring(), {opt.password, true, &cancel, opt.deadlineTick});
    fs::path decoded = workspace.root / L"decoded";
    auto decode = [&](const fs::path& input, const fs::path& output, const std::vector<Entry>& members) {
        std::set<std::wstring> names;
        std::set<std::wstring> fileNames;
        std::vector<std::wstring> normalizedNames;
        std::uint64_t maximum = 0;
        for (const auto& entry : members) {
            std::wstring path = entry.path;
            if (entry.isDir && !path.empty() && path.back() == L'/') path.pop_back();
            path = validatedPreviewPath(path);
            std::transform(path.begin(), path.end(), path.begin(),
                [](wchar_t character) { return static_cast<wchar_t>(std::towupper(character)); });
            if (entry.isLink || !names.insert(path).second)
                throw ArchiveError(diagnostic("压缩包包含链接或重复的目标路径"));
            if (!entry.isDir) fileNames.insert(path);
            normalizedNames.push_back(path);
            if (!entry.isDir) {
                if (!entry.sizeKnown || entry.size > std::numeric_limits<std::uint64_t>::max() - maximum)
                    throw ArchiveError(diagnostic("无法可靠确定解压所需空间"));
                maximum += entry.size;
            }
        }
        for (const auto& name : normalizedNames) {
            for (auto separator = name.find(L'/'); separator != std::wstring::npos;
                 separator = name.find(L'/', separator + 1)) {
                if (fileNames.count(name.substr(0, separator)))
                    throw ArchiveError(diagnostic("压缩包中的文件被用作父目录"));
            }
        }
        const auto available = fs::space(workspace.root).available;
        if (available < maximum || available - maximum < 16ull * 1024 * 1024)
            throw ArchiveError(diagnostic("磁盘空间不足，无法安全暂存完整解压结果"));
        files::makeDirectories(output);
        workspace.adopt(output);
        std::vector<std::wstring> arguments{ L"x", L"-o" + output.wstring(), L"-y", L"-spd", L"-ssc" };
        if (!opt.password.empty()) arguments.push_back(L"-p" + opt.password);
        arguments.push_back(L"--");
        arguments.push_back(input.wstring());
        std::uint64_t nextCheck = 0;
        auto monitor = [&] {
            if (GetTickCount64() < nextCheck) return;
            nextCheck = GetTickCount64() + 250;
            std::uint64_t actual = 0;
            for (const auto& item : fs::recursive_directory_iterator(output)) {
                const auto state = files::identity(item.path());
                if (state.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const auto size = item.file_size();
                if (size > maximum - actual) throw ArchiveError(diagnostic("解压实际输出超过声明大小，操作已停止"));
                actual += size;
            }
            if (fs::space(workspace.root).available < 16ull * 1024 * 1024)
                throw ArchiveError(diagnostic("磁盘剩余空间过低，操作已停止"));
        };
        try {
            runWithProgress(std::move(arguments), onProgress, cancel, diagnostic("解压"), opt.deadlineTick, monitor);
            monitor();
        }
        catch (const std::exception& error) { summary.outcome = Outcome::Failed; summary.diagnostic = error.what(); }
        if (cancel.load()) summary.outcome = Outcome::Cancelled;
        for (const auto& entry : members) {
            const fs::path path = output / fs::path(entry.path);
            if (files::identity(path).exists) workspace.adopt(path);
        }
    };
    decode(snapshot, decoded, entries);
    if (summary.outcome != Outcome::Completed && tarWrapped) {
        if (summary.outcome == Outcome::Failed) throw OperationError(summary);
        return summary;
    }
    if (summary.outcome == Outcome::Completed && tarWrapped) {
        std::vector<Entry> files;
        for (const auto& entry : entries) if (!entry.isDir) files.push_back(entry);
        if (files.size() != 1) throw ArchiveError(diagnostic("内层 tar 结构不明确"));
        const fs::path inner = decoded / fs::path(files.front().path);
        entries = list(inner.wstring(), {opt.password, true, &cancel, opt.deadlineTick});
        decoded = workspace.root / L"unwrapped";
        decode(inner, decoded, entries);
    }
    files::Resolver resolver(opt.conflict, opt.resolveConflict);
    std::vector<std::wstring> filters;
    for (auto wanted : include) {
        if (!wanted.empty() && (wanted.back() == L'/' || wanted.back() == L'\\')) wanted.pop_back();
        filters.push_back(validatedPreviewPath(wanted));
    }
    auto selected = [&](const std::wstring& member) {
        return filters.empty() || std::any_of(filters.begin(), filters.end(), [&](const auto& filter) {
            return member == filter || member.rfind(filter + L"/", 0) == 0;
        });
    };
    std::set<std::wstring> directoryNames;
    for (const auto& entry : entries) {
        std::wstring member = entry.path;
        if (entry.isDir && !member.empty() && member.back() == L'/') member.pop_back();
        member = validatedPreviewPath(member);
        if (!selected(member)) continue;
        if (entry.isDir) directoryNames.insert(member);
        for (auto separator = member.find(L'/'); separator != std::wstring::npos;
             separator = member.find(L'/', separator + 1))
            directoryNames.insert(member.substr(0, separator));
    }
    std::vector<std::wstring> orderedDirectories(directoryNames.begin(), directoryNames.end());
    std::sort(orderedDirectories.begin(), orderedDirectories.end(), [](const auto& first, const auto& second) {
        return first.size() < second.size();
    });
    std::map<std::wstring, fs::path> directoryTargets;
    std::set<std::wstring> skippedDirectories;
    auto foldedPath = [](const fs::path& path) {
        std::wstring value = path.wstring();
        std::transform(value.begin(), value.end(), value.begin(),
            [](wchar_t character) { return static_cast<wchar_t>(std::towupper(character)); });
        return value;
    };
    std::set<std::wstring> reserved;
    for (const auto& entry : entries) {
        std::wstring member = entry.path;
        if (entry.isDir && !member.empty() && member.back() == L'/') member.pop_back();
        if (selected(member)) reserved.insert(foldedPath(destination / fs::path(member)));
    }
    for (const auto& directory : directoryNames) reserved.insert(foldedPath(destination / fs::path(directory)));
    auto occupied = [&](const fs::path& candidate) {
        return reserved.count(foldedPath(candidate)) || files::identity(candidate).exists;
    };
    auto targetFor = [&](const std::wstring& member) {
        const auto separator = member.rfind(L'/');
        if (separator == std::wstring::npos) return destination / fs::path(member);
        const auto parent = member.substr(0, separator);
        if (skippedDirectories.count(parent)) return fs::path{};
        return directoryTargets.at(parent) / fs::path(member.substr(separator + 1));
    };
    for (const auto& directory : orderedDirectories) {
        fs::path target = targetFor(directory);
        if (target.empty()) { skippedDirectories.insert(directory); continue; }
        const auto state = files::identity(target);
        if (state.exists && !(state.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (summary.outcome != Outcome::Completed) { skippedDirectories.insert(directory); continue; }
            const auto decision = resolver.choose({directory, target.wstring(), true});
            if (cancel.load()) summary.outcome = Outcome::Cancelled;
            if (decision == ConflictPolicy::Skip || summary.outcome != Outcome::Completed) {
                skippedDirectories.insert(directory);
                continue;
            }
            if (decision != ConflictPolicy::Rename)
                throw ConflictError(diagnostic("目录与已有文件冲突，只能跳过或重命名: ") + utf8(directory));
            target = files::renamed(target, occupied);
            ++summary.renamed;
        }
        reserved.insert(foldedPath(target));
        directoryTargets[directory] = target;
    }
    struct Publication {
        const Entry* entry;
        fs::path staged;
        fs::path target;
        files::Identity previous;
        bool renamed = false;
        files::Identity verifiedState;
    };
    std::vector<Publication> publications;
    for (const auto& entry : entries) {
        std::wstring member = entry.path;
        if (entry.isDir && !member.empty() && member.back() == L'/') member.pop_back();
        const std::wstring normalized = validatedPreviewPath(member);
        if (!selected(normalized)) continue;
        const fs::path staged = decoded / fs::path(normalized);
        if (entry.isDir) continue;
        fs::path target = targetFor(normalized);
        if (target.empty()) { ++summary.skipped; continue; }
        if (zone) {
            const auto state = files::identity(staged);
            if (state.exists && !(state.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && state.info.nNumberOfLinks == 1)
                applySourceZone(staged.wstring(), zone);
        }
        files::Identity verifiedState;
        if (!files::verified(staged, entry, summary.outcome != Outcome::Completed, &cancel, &verifiedState)) {
            if (cancel.load()) { summary.outcome = Outcome::Cancelled; ++summary.skipped; continue; }
            if (summary.outcome == Outcome::Completed) {
                summary.outcome = Outcome::Failed;
                summary.diagnostic = diagnostic("解压结果缺失或校验失败: ") + utf8(entry.path);
            }
            continue;
        }
        auto previous = files::identity(target);
        ConflictPolicy decision = ConflictPolicy::Reject;
        bool rename = false;
        if (previous.exists) {
            if (summary.outcome != Outcome::Completed) { ++summary.skipped; continue; }
            decision = resolver.choose({entry.path, target.wstring(),
                (previous.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
            if (cancel.load()) { summary.outcome = Outcome::Cancelled; ++summary.skipped; continue; }
            if (decision == ConflictPolicy::Reject) throw ConflictError(diagnostic("目标已存在: ") + utf8(target.wstring()));
            if (decision == ConflictPolicy::Skip) { ++summary.skipped; continue; }
            if (decision == ConflictPolicy::Rename) {
                target = files::renamed(target, occupied);
                reserved.insert(foldedPath(target));
                previous = {};
                rename = true;
            } else {
                if (previous.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    throw ConflictError(diagnostic("不能覆盖已有目录"));
            }
        }
        publications.push_back({&entry, staged, target, previous, rename, verifiedState});
    }
    const bool crossVolume = !sameVolume(workspace.root, destination);
    for (const auto& publication : publications) {
        if (summary.outcome == Outcome::Completed && cancel.load()) summary.outcome = Outcome::Cancelled;
        if (crossVolume && cancel.load()) { summary.outcome = Outcome::Cancelled; ++summary.skipped; continue; }
        if (summary.outcome != Outcome::Completed && (publication.previous.exists ||
            !publication.entry->sizeKnown || !publication.entry->crcKnown)) { ++summary.skipped; continue; }
        try {
            files::commitFamily(workspace, {{publication.staged, publication.target}},
                publication.previous.exists ? std::vector<fs::path>{publication.target} : std::vector<fs::path>{},
                publication.previous.exists ? std::vector<files::Identity>{publication.previous} : std::vector<files::Identity>{},
                {publication.verifiedState}, crossVolume || summary.outcome == Outcome::Completed ? &cancel : nullptr);
        } catch (const OperationError& error) {
            summary.outcome = Outcome::Failed;
            summary.diagnostic = error.what();
            summary.recoveryPath = error.summary.recoveryPath;
            throw OperationError(summary);
        } catch (const std::exception& error) {
            if (cancel.load()) { summary.outcome = Outcome::Cancelled; break; }
            summary.outcome = Outcome::Failed;
            summary.diagnostic = error.what();
            throw OperationError(summary);
        }
        summary.paths.push_back(publication.target.wstring());
        ++summary.completed;
        if (publication.previous.exists) ++summary.overwritten;
        if (publication.renamed) ++summary.renamed;
    }
    if (summary.outcome == Outcome::Completed) {
        for (const auto& directory : directoryTargets) {
            if (cancel.load()) { summary.outcome = Outcome::Cancelled; break; }
            files::makeDirectories(directory.second);
        }
    }
    if (summary.outcome == Outcome::Failed) throw OperationError(summary);
    return summary;
}

void SevenZipEngine::extractToStream(const std::wstring& archive, const std::wstring& member,
                                    const ExtractOptions& opt, const DataFn& onData,
                                    const std::atomic_bool& cancel) const {
    const std::wstring wanted = validatedPreviewPath(member);
    if (!onData) throw ArchiveError(diagnostic("未提供预览数据接收器"));
    if (cancel.load()) throw ArchiveError(diagnostic("预览提取已取消"));
    ensure();
    const std::wstring real = resolveFirstVolume(archive);
    requireFile(real, diagnostic("压缩包"));
    std::wstring nativeMember = wanted;
    std::replace(nativeMember.begin(), nativeMember.end(), L'/', L'\\');
    auto arguments = [&](const std::wstring& command, bool streaming) {
        std::vector<std::wstring> args{ command, L"-spd", L"-ssc", L"-r-", L"-y", L"-bd", L"-bsp0" };
        if (streaming) args.push_back(L"-so");
        else {
            args.push_back(L"-slt");
            args.push_back(L"-sccUTF-8");
        }
        if (!opt.password.empty()) args.push_back(L"-p" + opt.password);
        args.push_back(L"-i!" + nativeMember);
        args.push_back(L"--");
        args.push_back(real);
        return args;
    };
    std::string metadata;
    const proc::RunResult listed = proc::run(exe_, arguments(L"l", false),
        [&](std::string_view chunk) {
            if (chunk.size() > 1024 * 1024 - metadata.size())
                throw ArchiveError(diagnostic("预览条目信息过大"));
            metadata.append(chunk);
            return true;
        }, cancel, {opt.deadlineTick});
    if (cancel.load()) throw ArchiveError(diagnostic("预览提取已取消"));
    if (listed.exitCode != 0)
        if (passwordFailure(listed.stderrTail) || passwordFailure(metadata)) throw PasswordError(diagnostic("预览需要正确的密码"));
        else throw ArchiveError(diagnostic("无法读取预览条目信息\n") + listed.stderrTail);
    const Listing listing = parseListing(metadata, true);
    if (listing.entries.size() != 1)
        throw ArchiveError(diagnostic("预览必须精确匹配一个条目，缺失或重复的条目不能预览"));
    const Entry& entry = listing.entries.front();
    if (entry.encrypted && opt.password.empty()) throw PasswordError(diagnostic("预览需要正确的密码"));
    if (entry.isDir || entry.isLink)
        throw ArchiveError(diagnostic("不允许预览目录、链接或重解析点条目"));
    if (validatedPreviewPath(entry.path) != wanted)
        throw ArchiveError(diagnostic("预览条目匹配不一致"));

    std::uint64_t received = 0;
    const proc::RunResult extracted = proc::run(exe_, arguments(L"x", true),
        [&](std::string_view chunk) {
            if (cancel.load()) throw ArchiveError(diagnostic("预览提取已取消"));
            if (entry.sizeKnown &&
                (received > entry.size || chunk.size() > entry.size - received))
                throw ArchiveError(diagnostic("预览数据超过声明的条目大小"));
            onData(chunk);
            received += chunk.size();
            return true;
        }, cancel, {opt.deadlineTick});
    if (cancel.load()) throw ArchiveError(diagnostic("预览提取已取消"));
    if (extracted.exitCode != 0)
        if (passwordFailure(extracted.stderrTail)) throw PasswordError(diagnostic("预览需要正确的密码"));
        else throw ArchiveError(diagnostic("无法提取预览条目\n") + extracted.stderrTail);
    if (entry.sizeKnown && received != entry.size)
        throw ArchiveError(diagnostic("预览数据长度与条目信息不一致"));
}

OperationSummary SevenZipEngine::create(const std::wstring& archive,
                            const std::vector<std::wstring>& inputs,
                            const CreateOptions& opt, const ProgressFn& onProgress,
                            const std::atomic_bool& cancel) const {
    OperationSummary result;
    fs::path target = fs::absolute(archive).lexically_normal();
    files::safeParents(target.parent_path());
    auto previous = files::family(target);
    std::vector<files::Identity> expected;
    for (const auto& path : previous) expected.push_back(files::identity(path));
    files::Resolver resolver(opt.conflict, opt.resolveConflict);
    if (!previous.empty()) {
        const auto policy = resolver.choose({L"", target.wstring(),
            std::any_of(expected.begin(), expected.end(), [](const auto& item) {
                return (item.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            })});
        if (cancel.load()) { result.outcome = Outcome::Cancelled; return result; }
        if (policy == ConflictPolicy::Reject) throw ConflictError(diagnostic("目标压缩包或分卷已存在"));
        if (policy == ConflictPolicy::Skip) { result.destination = target.wstring(); result.skipped = 1; return result; }
        if (policy == ConflictPolicy::Rename) {
            target = files::renamed(target, [](const fs::path& path) { return !files::family(path).empty(); });
            previous.clear();
            expected.clear();
            result.renamed = 1;
        } else if (std::any_of(expected.begin(), expected.end(), [](const auto& item) {
                return (item.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            })) throw ConflictError(diagnostic("不能覆盖目录"));
    }
    result.destination = target.wstring();
    files::Workspace workspace(target.parent_path());
    const fs::path staged = workspace.root / target.filename();
    std::vector<std::wstring> exclusions;
    for (const auto& input : inputs) {
        const std::wstring spelling = input;
        fs::path root(input);
        std::wstring prefix;
        if (spelling.find_first_of(L"*?") != std::wstring::npos) root = root.parent_path();
        else if (fs::is_directory(root)) prefix = root.filename().wstring();
        else {
            if (_wcsicmp(fs::absolute(root).wstring().c_str(), fs::absolute(archive).wstring().c_str()) == 0)
                throw ArchiveError(diagnostic("压缩输入不能是目标压缩包本身"));
            continue;
        }
        root = fs::absolute(root).lexically_normal();
        for (const auto& excluded : [&] {
                auto values = files::family(fs::absolute(archive));
                values.push_back(workspace.root);
                return values;
            }()) {
            const auto relative = excluded.lexically_relative(root);
            if (!relative.empty() && *relative.begin() != L"..")
                exclusions.push_back((fs::path(prefix) / relative).wstring());
        }
    }
    try { createRaw(staged.wstring(), inputs, opt, onProgress, cancel, exclusions); }
    catch (...) {
        for (const auto& path : files::family(staged)) workspace.adopt(path);
        if (cancel.load()) { result.outcome = Outcome::Cancelled; return result; }
        throw;
    }
    const auto produced = files::family(staged);
    for (const auto& path : produced) workspace.adopt(path);
    if (cancel.load()) { result.outcome = Outcome::Cancelled; return result; }
    if (produced.empty()) throw ArchiveError(diagnostic("压缩引擎没有生成结果"));
    std::vector<std::pair<fs::path, fs::path>> outputs;
    for (const auto& path : produced)
        outputs.push_back({path, target.parent_path() / path.filename()});
    try { files::commitFamily(workspace, outputs, previous, expected, {}, &cancel); }
    catch (const OperationError&) { throw; }
    catch (...) {
        if (cancel.load() && !workspace.preserve) { result.outcome = Outcome::Cancelled; return result; }
        throw;
    }
    for (const auto& item : outputs) result.paths.push_back(item.second.wstring());
    result.completed = produced.size();
    result.overwritten = previous.size();
    return result;
}

void SevenZipEngine::createRaw(const std::wstring& archive,
                            const std::vector<std::wstring>& inputs,
                            const CreateOptions& opt, const ProgressFn& onProgress,
                            const std::atomic_bool& cancel, const std::vector<std::wstring>& exclusions) const {
    ensure();
    if (inputs.empty()) throw ArchiveError(diagnostic("没有指定要压缩的文件或目录"));

    if (isTarWrappedExt(archive)) {
        // 两段式：先在临时目录打 tar，再套外层压缩（gzip/bzip2/xz/zstd）
        std::wstring tmpDir = fs::path(archive).parent_path().wstring();
        std::wstring tmpTar = tmpDir + L"\\tmp.tar";

        auto cleanup = [&]() {
            DeleteFileW(tmpTar.c_str());
        };

        std::vector<std::wstring> a{ L"a", tmpTar, L"-ttar", L"-y" };
        for (const auto& exclusion : exclusions) a.push_back(L"-xr!" + exclusion);
        if (opt.threads > 0) a.push_back(L"-mmt" + std::to_wstring(opt.threads));
        a.push_back(L"--");
        for (const auto& in : inputs) a.push_back(in);

        proc::RunResult r1 = proc::run(exe_, a,
            [](std::string_view) { return true; }, cancel);
        if (cancel.load()) { cleanup(); return; }
        if (r1.exitCode != 0) {
            cleanup();
            std::string msg = diagnostic("压缩失败 (打包 tar 阶段, exit ") + std::to_string(r1.exitCode) + ")";
            if (!r1.stderrTail.empty()) msg += "\n" + r1.stderrTail;
            throw ArchiveError(msg);
        }

        std::wstring e = lowerExtOf(archive);
        std::wstring t = (e == L".tar.bz2" || e == L".tbz" || e == L".tbz2") ? L"bzip2"
                       : (e == L".tar.xz" || e == L".txz") ? L"xz"
                       : (e == L".tar.zst" || e == L".tzst") ? L"zstd" : L"gzip";
        try {
            runWithProgress({ L"a", archive, L"-t" + t, L"-y", tmpTar },
                            onProgress, cancel, diagnostic("压缩"));
        } catch (...) { cleanup(); throw; }
        cleanup();
        return;
    }

    std::wstring type = archiveTypeFromExt(archive);
    const bool isSfx = (lowerExtOf(archive) == L".exe");
    if (type.empty() && isSfx) type = L"7z";   // .exe -> 7z 自解压包（-sfx）
    if (type.empty())
        throw ArchiveError(diagnostic("不支持的压缩格式: ") + utf8(archive) +
                    diagnostic("（当前支持 7z / zip / tar / gz / bz2 / xz / zst / tar.gz / tar.bz2 / tar.xz / exe(SFX) 等）"));

    // 单文件流格式（非 tar 包装）只支持单文件输入
    if ((type == L"gzip" || type == L"bzip2" || type == L"xz" || type == L"zstd") &&
        inputs.size() > 1)
        throw ArchiveError(utf8(type) + diagnostic(" 格式只支持单个文件；多文件请使用 .tar.gz / .tar.xz 等，或 7z / zip。"));

    std::vector<std::wstring> args{ L"a", archive, L"-t" + type, L"-y",
        L"-mx" + std::to_wstring(opt.level < 0 ? 5 : opt.level) };
    if (!opt.volumeSize.empty()) args.push_back(L"-v" + opt.volumeSize);
    if (isSfx) args.push_back(L"-sfx");
    for (const auto& exclusion : exclusions) args.push_back(L"-xr!" + exclusion);
    if (opt.threads > 0) args.push_back(L"-mmt" + std::to_wstring(opt.threads));
    if (!opt.password.empty()) {
        args.push_back(L"-p" + opt.password);
        if (type == L"7z") args.push_back(L"-mhe=on");
    }
    args.push_back(L"--");
    for (const auto& in : inputs) args.push_back(in);
    runWithProgress(std::move(args), onProgress, cancel, diagnostic("压缩"));
}

void SevenZipEngine::test(const std::wstring& archive, const std::wstring& password,
                          const ProgressFn& onProgress,
                          const std::atomic_bool& cancel) const {
    ensure();
    const std::wstring real = resolveFirstVolume(archive);
    requireFile(real, diagnostic("压缩包"));
    std::vector<std::wstring> args{ L"t", L"-y" };
    if (!password.empty()) args.push_back(L"-p" + password);
    args.push_back(L"--");
    args.push_back(real);
    runWithProgress(std::move(args), onProgress, cancel, diagnostic("完整性测试"));
}

} // namespace ggzip

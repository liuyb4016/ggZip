#include "FileTransaction.h"
#include "ggzip/OperationError.h"
#include "ggzip/Diagnostics.h"
#include "ggzip/Text.h"
#include "ggzip/SourceMark.h"
#include <algorithm>
#include <fstream>
#include <array>

namespace fs = std::filesystem;

namespace ggzip::files {
namespace {

constexpr std::size_t maximumCachedDirectories = 64;
constexpr std::ptrdiff_t maximumCachedDirectoryDepth = 32;

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

std::wstring nativePath(const fs::path& path) {
    std::wstring value = fs::absolute(path).make_preferred().wstring();
    if (value.size() < 248 || value.rfind(L"\\\\?\\", 0) == 0) return value;
    if (value.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + value.substr(2);
    return L"\\\\?\\" + value;
}

bool equalObject(const BY_HANDLE_FILE_INFORMATION& first, const BY_HANDLE_FILE_INFORMATION& second) {
    return first.dwVolumeSerialNumber == second.dwVolumeSerialNumber &&
           first.nFileIndexHigh == second.nFileIndexHigh && first.nFileIndexLow == second.nFileIndexLow;
}

void fail(const char* operation) {
    throw ArchiveError(std::string(operation) + " (Windows " + std::to_string(GetLastError()) + ")");
}

}

Identity identity(const fs::path& path) {
    Identity result;
    const DWORD attributes = GetFileAttributesW(nativePath(path).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) return result;
        fail(diagnostic("无法检查目标"));
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) throw ArchiveError(diagnostic("拒绝链接或重解析点目标: ") + utf8(path.wstring()));
    const Handle handle(CreateFileW(nativePath(path).c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (handle.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle.value, &result.info))
        fail(diagnostic("无法检查文件身份"));
    if (result.info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        throw ArchiveError(diagnostic("文件在检查时变为重解析点"));
    result.exists = true;
    return result;
}

bool same(const Identity& first, const Identity& second) {
    return first.exists == second.exists && (!first.exists ||
        (equalObject(first.info, second.info) &&
         first.info.nFileSizeHigh == second.info.nFileSizeHigh && first.info.nFileSizeLow == second.info.nFileSizeLow &&
         CompareFileTime(&first.info.ftLastWriteTime, &second.info.ftLastWriteTime) == 0));
}

void safeParents(const fs::path& path) {
    const fs::path absolute = fs::absolute(path).lexically_normal();
    fs::path current = absolute.root_path();
    for (const auto& component : absolute.relative_path()) {
        current /= component;
        const Identity state = identity(current);
        if (state.exists && !(state.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            throw ArchiveError(diagnostic("目标父路径不是普通目录: ") + utf8(current.wstring()));
    }
}

void makeDirectories(const fs::path& path) {
    DirectoryLock directories(path, true);
}

std::vector<fs::path> family(const fs::path& base) {
    std::vector<fs::path> result;
    if (identity(base).exists) result.push_back(base);
    const fs::path parent = fs::absolute(base).parent_path();
    const std::wstring prefix = base.filename().wstring() + L".";
    if (!fs::exists(parent)) return result;
    for (const auto& item : fs::directory_iterator(parent)) {
        const std::wstring name = item.path().filename().wstring();
        if (name.size() <= prefix.size() || _wcsnicmp(name.c_str(), prefix.c_str(), prefix.size()) != 0) continue;
        const std::wstring suffix = name.substr(prefix.size());
        if (suffix.size() >= 3 && std::all_of(suffix.begin(), suffix.end(),
                [](wchar_t character) { return character >= L'0' && character <= L'9'; }))
            result.push_back(item.path());
    }
    std::sort(result.begin(), result.end());
    return result;
}

fs::path renamed(const fs::path& path, const std::function<bool(const fs::path&)>& occupied) {
    std::wstring filename = path.filename().wstring();
    std::wstring extension = path.extension().wstring();
    for (const auto& compound : { L".tar.gz", L".tar.bz2", L".tar.xz", L".tar.zst" }) {
        const std::wstring ending(compound);
        if (filename.size() >= ending.size() &&
            _wcsicmp(filename.substr(filename.size() - ending.size()).c_str(), ending.c_str()) == 0) {
            extension = ending;
            break;
        }
    }
    const std::wstring stem = filename.substr(0, filename.size() - extension.size());
    for (std::uint64_t index = 1; index < 1000000; ++index) {
        const fs::path candidate = path.parent_path() / (stem + L" (" + std::to_wstring(index) + L")" + extension);
        if (!occupied(candidate)) return candidate;
    }
    throw ConflictError(diagnostic("无法生成不冲突的名称"));
}

bool verified(const fs::path& path, const Entry& entry, bool requireCrc, const std::atomic_bool* cancel,
              Identity* verifiedState) {
    try {
        if (cancel && cancel->load()) return false;
        DirectoryLock directories(path.parent_path());
        const Handle input(CreateFileW(nativePath(path).c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        BY_HANDLE_FILE_INFORMATION state{};
        if (input.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(input.value, &state)) return false;
        const auto size = (static_cast<std::uint64_t>(state.nFileSizeHigh) << 32) | state.nFileSizeLow;
        if ((state.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            state.nNumberOfLinks != 1 ||
            (entry.sizeKnown && size != entry.size) ||
            (requireCrc && (!entry.crcKnown || !entry.sizeKnown))) return false;
        if (!entry.crcKnown) { if (verifiedState) *verifiedState = {true, state}; return true; }
        static const auto table = [] {
            std::array<std::uint32_t, 256> values{};
            for (std::uint32_t index = 0; index < values.size(); ++index) {
                auto checksum = index;
                for (int bit = 0; bit < 8; ++bit)
                    checksum = (checksum >> 1) ^ ((checksum & 1) ? 0xedb88320 : 0);
                values[index] = checksum;
            }
            return values;
        }();
        std::uint32_t checksum = 0xffffffff;
        char buffer[65536];
        std::uint64_t received = 0;
        for (;;) {
            if (cancel && cancel->load()) return false;
            DWORD count = 0;
            if (!ReadFile(input.value, buffer, sizeof(buffer), &count, nullptr)) return false;
            if (!count) break;
            received += count;
            for (DWORD index = 0; index < count; ++index) {
                checksum = (checksum >> 8) ^ table[(checksum ^ static_cast<unsigned char>(buffer[index])) & 255];
            }
        }
        const bool matched = received == size && ~checksum == entry.crc32;
        if (matched && verifiedState) *verifiedState = {true, state};
        return matched;
    } catch (...) { return false; }
}

Workspace::Workspace(const fs::path& parent, Purpose purpose) {
    fs::path ancestor = fs::absolute(parent).lexically_normal();
    while (!fs::exists(ancestor)) {
        const fs::path next = ancestor.parent_path();
        if (next == ancestor) throw ArchiveError(diagnostic("暂存目录不可用"));
        ancestor = next;
    }
    root = purpose == Purpose::PrivateStaging
        ? createStagingDirectory(ancestor, L"ggZip-job-", lock_)
        : createPublicationDirectory(ancestor, L"ggZip-publish-", lock_);
    rootState_ = identity(root);
}

void Workspace::adopt(const fs::path& path) {
    if (!lock_ || !lock_->intact()) throw ArchiveError(diagnostic("操作目录状态发生不安全变化"));
    const fs::path absolute = fs::absolute(path).lexically_normal();
    const fs::path relative = absolute.lexically_relative(root);
    if (relative.empty() || relative == L"." || *relative.begin() == L"..")
        throw ArchiveError(diagnostic("操作文件越界"));
    safeParents(absolute.parent_path());
    for (fs::path parent = absolute.parent_path(); parent != root; parent = parent.parent_path()) {
        if (!recordIndex_.count(parent)) {
            const auto state = identity(parent);
            recordIndex_.emplace(parent, records_.size());
            records_.push_back({parent, state});
        }
    }
    if (!recordIndex_.count(absolute)) {
        const auto state = identity(absolute);
        recordIndex_.emplace(absolute, records_.size());
        records_.push_back({absolute, state});
    }
}

void Workspace::adoptOutput(const fs::path& path) {
    const auto rootState = identity(root);
    if (!rootState.exists || !equalObject(rootState.info, rootState_.info))
        throw ArchiveError(diagnostic("操作目录的身份发生变化"));
    adopt(path);
    const auto absolute = fs::absolute(path).lexically_normal();
    const auto state = identity(absolute);
    if (!state.exists || (state.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || state.info.nNumberOfLinks != 1)
        throw ArchiveError(diagnostic("引擎输出不是独占的普通文件"));
    records_[recordIndex_.at(absolute)].state = state;
}

fs::path Workspace::snapshot(const fs::path& source, const fs::path& relative,
                            const std::atomic_bool& cancel) {
    if (!lock_ || !lock_->intact()) throw ArchiveError(diagnostic("输入快照目录状态发生变化"));
    safeParents(source.parent_path());
    const Handle input(CreateFileW(nativePath(source).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (input.value == INVALID_HANDLE_VALUE || GetFileType(input.value) != FILE_TYPE_DISK)
        fail(diagnostic("无法锁定输入文件"));
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(input.value, &information) ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        throw ArchiveError(diagnostic("输入快照仅支持普通文件"));
    const fs::path destination = root / relative;
    const auto contained = fs::absolute(destination).lexically_normal().lexically_relative(root);
    if (contained.empty() || contained == L"." || *contained.begin() == L"..")
        throw ArchiveError(diagnostic("输入快照路径越界"));
    makeDirectories(destination.parent_path());
    const Handle output(CreateFileW(nativePath(destination).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (output.value == INVALID_HANDLE_VALUE) fail(diagnostic("无法创建输入快照"));
    adopt(destination);
    char buffer[65536];
    DWORD count = 0;
    for (;;) {
        if (cancel.load()) throw ArchiveError(diagnostic("操作已取消"));
        if (!ReadFile(input.value, buffer, sizeof(buffer), &count, nullptr)) fail(diagnostic("读取输入失败"));
        if (!count) break;
        DWORD offset = 0;
        while (offset < count) {
            DWORD written = 0;
            if (!WriteFile(output.value, buffer + offset, count - offset, &written, nullptr) || !written)
                fail(diagnostic("写入输入快照失败"));
            offset += written;
        }
    }
    if (!FlushFileBuffers(output.value)) fail(diagnostic("保存输入快照失败"));
    return destination;
}

fs::path Workspace::write(const fs::path& relative, std::string_view bytes) {
    if (!lock_ || !lock_->intact()) throw ArchiveError(diagnostic("操作文件目录状态发生变化"));
    const auto destination = fs::absolute(root / relative).lexically_normal();
    const auto contained = destination.lexically_relative(root);
    if (contained.empty() || contained == L"." || *contained.begin() == L"..")
        throw ArchiveError(diagnostic("操作文件越界"));
    makeDirectories(destination.parent_path());
    const Handle output(CreateFileW(nativePath(destination).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (output.value == INVALID_HANDLE_VALUE) fail(diagnostic("无法创建操作文件"));
    adopt(destination);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD count = static_cast<DWORD>(std::min<std::size_t>(65536, bytes.size() - offset));
        DWORD written = 0;
        if (!WriteFile(output.value, bytes.data() + offset, count, &written, nullptr) || !written)
            fail(diagnostic("无法写入操作文件"));
        offset += written;
    }
    if (!FlushFileBuffers(output.value)) fail(diagnostic("无法保存操作文件"));
    return destination;
}

std::shared_ptr<DirectoryLock> Workspace::directoryLock(const fs::path& path, bool create) {
    if (!lock_ || !lock_->intact()) throw ArchiveError(diagnostic("操作目录状态发生不安全变化"));
    const auto absolute = fs::absolute(path).lexically_normal();
    const auto cached = directoryLocks_.find(absolute);
    if (cached != directoryLocks_.end()) {
        if (!cached->second->intact()) throw ArchiveError(diagnostic("操作目录状态发生不安全变化"));
        return cached->second;
    }
    auto directory = std::make_shared<DirectoryLock>(absolute, create);
    if (directoryLocks_.size() < maximumCachedDirectories &&
        std::distance(absolute.begin(), absolute.end()) <= maximumCachedDirectoryDepth)
        directoryLocks_.emplace(absolute, directory);
    return directory;
}

Workspace::~Workspace() {
    directoryLocks_.clear();
    if (preserve) return;
    try {
        const Identity currentRoot = identity(root);
        if (!currentRoot.exists || !equalObject(currentRoot.info, rootState_.info)) return;
        std::sort(records_.begin(), records_.end(), [](const Record& first, const Record& second) {
            return first.path.native().size() > second.path.native().size();
        });
        for (const auto& record : records_) {
            try {
                const Identity current = identity(record.path);
                if (!current.exists || !equalObject(current.info, record.state.info)) continue;
                DirectoryLock parents(record.path.parent_path());
                const Handle handle(CreateFileW(nativePath(record.path).c_str(), DELETE | FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
                BY_HANDLE_FILE_INFORMATION info{};
                if (handle.value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle.value, &info) &&
                    equalObject(info, record.state.info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                    ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.nNumberOfLinks == 1)) {
                    FILE_DISPOSITION_INFO disposition{TRUE};
                    SetFileInformationByHandle(handle.value, FileDispositionInfo, &disposition, sizeof(disposition));
                }
            } catch (...) {}
        }
        lock_.reset();
        DirectoryLock parents(root.parent_path());
        const Handle handle(CreateFileW(nativePath(root).c_str(), DELETE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (handle.value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle.value, &info) &&
            equalObject(info, rootState_.info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            FILE_DISPOSITION_INFO disposition{TRUE};
            SetFileInformationByHandle(handle.value, FileDispositionInfo, &disposition, sizeof(disposition));
        }
    } catch (...) {}
}

Resolver::Resolver(ConflictPolicy policy, ConflictFn callback)
    : policy_(policy), callback_(std::move(callback)) {}

ConflictPolicy Resolver::choose(const Conflict& conflict) {
    if (policy_ != ConflictPolicy::Ask) return policy_;
    const bool remembered = conflict.directoryConflict ? allDirectories_ : allFiles_;
    if (remembered) return conflict.directoryConflict ? directories_ : files_;
    if (!callback_) return ConflictPolicy::Reject;
    const auto decision = callback_(conflict);
    if (decision.applyToAll) {
        if (conflict.directoryConflict) { allDirectories_ = true; directories_ = decision.policy; }
        else { allFiles_ = true; files_ = decision.policy; }
    }
    return decision.policy == ConflictPolicy::Ask ? ConflictPolicy::Reject : decision.policy;
}

namespace {

void checkCancellation(const std::atomic_bool* cancel) {
    if (cancel && cancel->load()) throw ArchiveError(diagnostic("操作已取消"));
}

std::unique_ptr<Handle> copyForPublication(Workspace& workspace, const fs::path& source,
                                         const fs::path& destination, const Identity* expected,
                                         const std::atomic_bool* cancel) {
    DirectoryLock sourceDirectory(source.parent_path());
    const Handle input(CreateFileW(nativePath(source).c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    BY_HANDLE_FILE_INFORMATION information{};
    if (input.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(input.value, &information) ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        information.nNumberOfLinks != 1) throw ArchiveError(diagnostic("无法锁定跨卷提交的已校验文件"));
    if (expected && !same({true, information}, *expected))
        throw ArchiveError(diagnostic("暂存文件在校验后发生变化，未提交结果"));
    const unsigned zone = sourceZone(source.wstring());
    if (zone && !fileSystemCapabilities(destination.parent_path()).namedStreams)
        throw ArchiveError(diagnostic("目标文件系统不支持下载来源标记，文件未发布"));
    auto output = std::make_unique<Handle>(CreateFileW(nativePath(destination).c_str(),
        DELETE | GENERIC_READ | GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (output->value == INVALID_HANDLE_VALUE) fail(diagnostic("无法创建跨卷提交文件"));
    try {
        workspace.adoptOutput(destination);
        char buffer[65536];
        for (;;) {
            checkCancellation(cancel);
            DWORD count = 0;
            if (!ReadFile(input.value, buffer, sizeof(buffer), &count, nullptr)) fail(diagnostic("读取跨卷提交文件失败"));
            if (!count) break;
            DWORD offset = 0;
            while (offset < count) {
                DWORD written = 0;
                if (!WriteFile(output->value, buffer + offset, count - offset, &written, nullptr) || !written)
                    fail(diagnostic("写入跨卷提交文件失败"));
                offset += written;
            }
        }
        if (!FlushFileBuffers(output->value)) fail(diagnostic("保存跨卷提交文件失败"));
        LARGE_INTEGER start{};
        if (!SetFilePointerEx(input.value, start, nullptr, FILE_BEGIN) ||
            !SetFilePointerEx(output->value, start, nullptr, FILE_BEGIN)) fail(diagnostic("无法校验跨卷提交文件"));
        char comparison[65536];
        for (;;) {
            checkCancellation(cancel);
            DWORD sourceBytes = 0;
            DWORD outputBytes = 0;
            if (!ReadFile(input.value, buffer, sizeof(buffer), &sourceBytes, nullptr) ||
                !ReadFile(output->value, comparison, sizeof(comparison), &outputBytes, nullptr) ||
                sourceBytes != outputBytes ||
                !std::equal(buffer, buffer + sourceBytes, comparison))
                throw ArchiveError(diagnostic("跨卷提交副本未通过完整内容校验"));
            if (!sourceBytes) break;
        }
        applySourceZone(destination.wstring(), zone);
        workspace.adoptOutput(destination);
        checkCancellation(cancel);
    } catch (...) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(output->value, FileDispositionInfo, &disposition, sizeof(disposition));
        throw;
    }
    return output;
}

void commitLocal(Workspace& workspace, const std::vector<std::pair<fs::path, fs::path>>& outputs,
                  const std::vector<fs::path>& previous, const std::vector<Identity>& expected,
                  const std::vector<Identity>& expectedOutputs, const std::atomic_bool* cancel,
                  std::vector<std::unique_ptr<Handle>>* prepared = nullptr) {
    static std::atomic_uint64_t sequence{0};
    fs::path backup;
    std::unique_ptr<DirectoryLock> backupDirectory;
    if (!previous.empty()) {
        backup = workspace.root / (L"rollback-" + std::to_wstring(++sequence));
        makeDirectories(backup);
        workspace.adopt(backup);
        backupDirectory = std::make_unique<DirectoryLock>(backup);
    }
    std::vector<std::unique_ptr<Handle>> handles;
    std::vector<fs::path> backups;
    std::vector<std::unique_ptr<Handle>> committed;
    std::vector<std::shared_ptr<DirectoryLock>> directories;
    std::vector<std::shared_ptr<DirectoryLock>> originalDirectories;
    std::vector<std::shared_ptr<DirectoryLock>> sourceDirectories;
    const fs::path journal = workspace.root / L"commit.txt";
    std::ofstream log(fs::path(nativePath(journal)), std::ios::binary);
    if (!log) throw ArchiveError(diagnostic("无法创建事务恢复记录"));
    workspace.adopt(journal);
    for (const auto& item : outputs) log << "NEW " << utf8(item.second.wstring()) << "\n";
    for (const auto& path : previous) log << "OLD " << utf8(path.wstring()) << "\n";
    log.flush();
    if (!log) throw ArchiveError(diagnostic("无法保存事务恢复记录"));
    try {
        checkCancellation(cancel);
        if (previous.size() != expected.size()) throw ArchiveError(diagnostic("事务目标身份数量不一致"));
        if (!expectedOutputs.empty() && expectedOutputs.size() != outputs.size())
            throw ArchiveError(diagnostic("已校验文件身份数量不一致"));
        for (const auto& item : outputs) {
            auto directory = workspace.directoryLock(item.second.parent_path(), true);
            directory->requireWritable();
            directories.push_back(std::move(directory));
            sourceDirectories.push_back(workspace.directoryLock(item.first.parent_path()));
        }
        for (std::size_t index = 0; index < previous.size(); ++index) {
            checkCancellation(cancel);
            originalDirectories.push_back(workspace.directoryLock(previous[index].parent_path()));
            if (!same(identity(previous[index]), expected[index]))
                throw ConflictError(diagnostic("目标在确认后发生变化"));
            auto handle = std::make_unique<Handle>(CreateFileW(nativePath(previous[index]).c_str(),
                DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (handle->value == INVALID_HANDLE_VALUE) fail(diagnostic("无法锁定原目标"));
            BY_HANDLE_FILE_INFORMATION current{};
            if (!GetFileInformationByHandle(handle->value, &current) ||
                !same({true, current}, expected[index])) throw ConflictError(diagnostic("目标身份已改变"));
            const fs::path saved = backup / (std::to_wstring(index) + L".old");
            backupDirectory->renameFile(handle->value, saved.filename().wstring());
            backups.push_back(saved);
            handles.push_back(std::move(handle));
        }
        for (std::size_t index = 0; index < outputs.size(); ++index) {
            checkCancellation(cancel);
            const auto& item = outputs[index];
            if (!sourceDirectories[index]->intact()) throw ArchiveError(diagnostic("暂存目录发生不安全变化"));
            safeParents(item.first.parent_path());
            auto handle = prepared ? std::move((*prepared)[index])
                : std::make_unique<Handle>(CreateFileW(nativePath(item.first).c_str(), DELETE | GENERIC_READ | FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            BY_HANDLE_FILE_INFORMATION information{};
            if (handle->value == INVALID_HANDLE_VALUE ||
                !GetFileInformationByHandle(handle->value, &information) ||
                (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
                information.nNumberOfLinks != 1)
                throw ArchiveError(diagnostic("无法锁定待提交文件"));
            if (!expectedOutputs.empty() && !same({true, information}, expectedOutputs[index]))
                throw ArchiveError(diagnostic("暂存文件在校验后发生变化，未提交结果"));
            directories[index]->renameFile(handle->value, item.second.filename().wstring());
            committed.push_back(std::move(handle));
        }
        checkCancellation(cancel);
    } catch (...) {
        bool restored = true;
        for (std::size_t index = committed.size(); index > 0; --index) {
            try { sourceDirectories[index - 1]->renameFile(committed[index - 1]->value,
                outputs[index - 1].first.filename().wstring()); }
            catch (...) { restored = false; }
        }
        for (std::size_t index = 0; index < handles.size(); ++index) {
            try { originalDirectories[index]->renameFile(handles[index]->value, previous[index].filename().wstring()); }
            catch (...) { restored = false; }
        }
        if (!restored) {
            workspace.preserve = true;
            OperationSummary result;
            result.outcome = Outcome::Failed;
            result.recoveryPath = workspace.root.wstring();
            result.diagnostic = diagnostic("提交或恢复失败，原文件备份保留于: ") + utf8(result.recoveryPath);
            throw OperationError(std::move(result));
        }
        throw;
    }
    handles.clear();
    for (const auto& path : backups) workspace.adopt(path);
}

}

void commitFamily(Workspace& workspace, const std::vector<std::pair<fs::path, fs::path>>& outputs,
                  const std::vector<fs::path>& previous, const std::vector<Identity>& expected,
                  const std::vector<Identity>& expectedOutputs, const std::atomic_bool* cancel) {
    if (previous.size() != expected.size() ||
        (!expectedOutputs.empty() && expectedOutputs.size() != outputs.size()))
        throw ArchiveError(diagnostic("事务文件身份数量不一致"));
    if (outputs.empty()) {
        if (!previous.empty()) throw ArchiveError(diagnostic("事务缺少待提交文件"));
        return;
    }
    checkCancellation(cancel);
    const auto destination = outputs.front().second.parent_path();
    const auto sourceVolume = workspace.directoryLock(workspace.root)->volume();
    const auto destinationVolume = workspace.directoryLock(destination, true)->volume();
    auto onVolume = [](const std::wstring& first, const std::wstring& second) {
        return _wcsicmp(first.c_str(), second.c_str()) == 0;
    };
    bool local = onVolume(sourceVolume, destinationVolume);
    for (const auto& item : outputs) {
        checkCancellation(cancel);
        if (!onVolume(destinationVolume, workspace.directoryLock(item.second.parent_path(), true)->volume()))
            throw ArchiveError(diagnostic("一个事务不能跨多个目标卷"));
        local = local && onVolume(sourceVolume, workspace.directoryLock(item.first.parent_path())->volume());
    }
    for (const auto& path : previous)
        if (!onVolume(destinationVolume, workspace.directoryLock(path.parent_path())->volume()))
            throw ArchiveError(diagnostic("原目标与提交目标不在同一卷"));
    if (local) {
        commitLocal(workspace, outputs, previous, expected, expectedOutputs, cancel);
        return;
    }
    checkCancellation(cancel);
    Workspace publication(destination, Workspace::Purpose::Publication);
    std::vector<std::pair<fs::path, fs::path>> copied;
    std::vector<Identity> states;
    std::vector<std::unique_ptr<Handle>> handles;
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        const auto path = publication.root / (std::to_wstring(index) + L".ready");
        handles.push_back(copyForPublication(publication, outputs[index].first, path,
            expectedOutputs.empty() ? nullptr : &expectedOutputs[index], cancel));
        BY_HANDLE_FILE_INFORMATION state{};
        if (!GetFileInformationByHandle(handles.back()->value, &state)) fail(diagnostic("无法读取跨卷副本身份"));
        states.push_back({true, state});
        copied.push_back({path, outputs[index].second});
    }
    commitLocal(publication, copied, previous, expected, states, cancel, &handles);
}

}

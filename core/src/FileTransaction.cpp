#include "FileTransaction.h"
#include "ggzip/OperationError.h"
#include "ggzip/Text.h"
#include <bcrypt.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <set>
#include <array>

namespace fs = std::filesystem;

namespace ggzip::files {
namespace {

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

bool equalObject(const BY_HANDLE_FILE_INFORMATION& first, const BY_HANDLE_FILE_INFORMATION& second) {
    return first.dwVolumeSerialNumber == second.dwVolumeSerialNumber &&
           first.nFileIndexHigh == second.nFileIndexHigh && first.nFileIndexLow == second.nFileIndexLow;
}

void fail(const char* operation) {
    throw ArchiveError(std::string(operation) + " (Windows " + std::to_string(GetLastError()) + ")");
}

void renameHandle(HANDLE handle, const fs::path& destination) {
    const std::wstring name = fs::absolute(destination).wstring();
    const std::size_t bytes = sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t);
    std::vector<unsigned char> buffer(bytes, 0);
    auto* information = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    information->ReplaceIfExists = FALSE;
    information->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::copy(name.begin(), name.end(), information->FileName);
    if (!SetFileInformationByHandle(handle, FileRenameInfo, information, static_cast<DWORD>(bytes)))
        fail("无法移动受保护文件");
}

std::vector<std::unique_ptr<Handle>> lockParents(const fs::path& path) {
    std::vector<std::unique_ptr<Handle>> handles;
    const fs::path absolute = fs::absolute(path).lexically_normal();
    fs::path current = absolute.root_path();
    for (const auto& component : absolute.relative_path()) {
        current /= component;
        auto handle = std::make_unique<Handle>(CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION information{};
        if (handle->value == INVALID_HANDLE_VALUE ||
            !GetFileInformationByHandle(handle->value, &information) ||
            !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw ArchiveError("无法锁定安全目标目录: " + utf8(current.wstring()));
        handles.push_back(std::move(handle));
    }
    return handles;
}

}

Identity identity(const fs::path& path) {
    Identity result;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) return result;
        fail("无法检查目标");
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) throw ArchiveError("拒绝链接或重解析点目标: " + utf8(path.wstring()));
    const Handle handle(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (handle.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle.value, &result.info))
        fail("无法检查文件身份");
    if (result.info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        throw ArchiveError("文件在检查时变为重解析点");
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
            throw ArchiveError("目标父路径不是普通目录: " + utf8(current.wstring()));
    }
}

void makeDirectories(const fs::path& path) {
    safeParents(path);
    fs::create_directories(path);
    safeParents(path);
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
    throw ConflictError("无法生成不冲突的名称");
}

bool verified(const fs::path& path, const Entry& entry, bool requireCrc) {
    try {
        const Identity state = identity(path);
        if (!state.exists || (state.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (entry.sizeKnown && fs::file_size(path) != entry.size) ||
            (requireCrc && (!entry.crcKnown || !entry.sizeKnown))) return false;
        if (!entry.crcKnown) return true;
        std::ifstream input(path, std::ios::binary);
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
        while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
            for (std::streamsize index = 0; index < input.gcount(); ++index) {
                checksum = (checksum >> 8) ^ table[(checksum ^ static_cast<unsigned char>(buffer[index])) & 255];
            }
        }
        return input.eof() && ~checksum == entry.crc32;
    } catch (...) { return false; }
}

Workspace::Workspace(const fs::path& parent) {
    fs::path ancestor = fs::absolute(parent).lexically_normal();
    while (!fs::exists(ancestor)) {
        const fs::path next = ancestor.parent_path();
        if (next == ancestor) throw ArchiveError("暂存目录不可用");
        ancestor = next;
    }
    safeParents(ancestor);
    unsigned char random[16]{};
    if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw ArchiveError("无法生成安全操作目录");
    std::wostringstream name;
    name << L"ggZip-job-" << std::hex << std::setfill(L'0');
    for (const unsigned char byte : random) name << std::setw(2) << static_cast<unsigned int>(byte);
    root = ancestor / name.str();
    if (!fs::create_directory(root)) throw ArchiveError("操作目录已存在");
    rootState_ = identity(root);
}

void Workspace::adopt(const fs::path& path) {
    const fs::path absolute = fs::absolute(path).lexically_normal();
    const fs::path relative = absolute.lexically_relative(root);
    if (relative.empty() || relative == L"." || *relative.begin() == L"..")
        throw ArchiveError("操作文件越界");
    safeParents(absolute.parent_path());
    for (fs::path parent = absolute.parent_path(); parent != root; parent = parent.parent_path()) {
        const auto found = std::find_if(records_.begin(), records_.end(),
            [&](const Record& record) { return record.path == parent; });
        if (found == records_.end()) records_.push_back({parent, identity(parent)});
    }
    if (std::none_of(records_.begin(), records_.end(),
            [&](const Record& record) { return record.path == absolute; }))
        records_.push_back({absolute, identity(absolute)});
}

fs::path Workspace::snapshot(const fs::path& source, const fs::path& relative,
                            const std::atomic_bool& cancel) {
    safeParents(source.parent_path());
    const Handle input(CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (input.value == INVALID_HANDLE_VALUE || GetFileType(input.value) != FILE_TYPE_DISK)
        fail("无法锁定输入文件");
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(input.value, &information) ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        throw ArchiveError("输入快照仅支持普通文件");
    const fs::path destination = root / relative;
    makeDirectories(destination.parent_path());
    const Handle output(CreateFileW(destination.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (output.value == INVALID_HANDLE_VALUE) fail("无法创建输入快照");
    adopt(destination);
    char buffer[65536];
    DWORD count = 0;
    for (;;) {
        if (cancel.load()) throw ArchiveError("操作已取消");
        if (!ReadFile(input.value, buffer, sizeof(buffer), &count, nullptr)) fail("读取输入失败");
        if (!count) break;
        DWORD offset = 0;
        while (offset < count) {
            DWORD written = 0;
            if (!WriteFile(output.value, buffer + offset, count - offset, &written, nullptr) || !written)
                fail("写入输入快照失败");
            offset += written;
        }
    }
    if (!FlushFileBuffers(output.value)) fail("保存输入快照失败");
    return destination;
}

Workspace::~Workspace() {
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
                if (current.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    RemoveDirectoryW(record.path.c_str());
                    continue;
                }
                const Handle handle(CreateFileW(record.path.c_str(), DELETE | FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
                BY_HANDLE_FILE_INFORMATION info{};
                if (handle.value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle.value, &info) &&
                    equalObject(info, record.state.info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    FILE_DISPOSITION_INFO disposition{TRUE};
                    SetFileInformationByHandle(handle.value, FileDispositionInfo, &disposition, sizeof(disposition));
                }
            } catch (...) {}
        }
        RemoveDirectoryW(root.c_str());
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

void commitFamily(Workspace& workspace, const std::vector<std::pair<fs::path, fs::path>>& outputs,
                  const std::vector<fs::path>& previous, const std::vector<Identity>& expected) {
    static std::atomic_uint64_t sequence{0};
    const fs::path backup = workspace.root / (L"rollback-" + std::to_wstring(++sequence));
    makeDirectories(backup);
    workspace.adopt(backup);
    std::vector<std::unique_ptr<Handle>> handles;
    std::vector<fs::path> backups;
    std::vector<std::unique_ptr<Handle>> committed;
    std::vector<std::vector<std::unique_ptr<Handle>>> directories;
    const fs::path journal = workspace.root / L"commit.txt";
    std::ofstream log(journal, std::ios::binary);
    workspace.adopt(journal);
    for (const auto& item : outputs) log << "NEW " << utf8(item.second.wstring()) << "\n";
    for (const auto& path : previous) log << "OLD " << utf8(path.wstring()) << "\n";
    log.flush();
    try {
        if (previous.size() != expected.size()) throw ArchiveError("事务目标身份数量不一致");
        for (const auto& item : outputs) {
            makeDirectories(item.second.parent_path());
            directories.push_back(lockParents(item.second.parent_path()));
        }
        for (std::size_t index = 0; index < previous.size(); ++index) {
            safeParents(previous[index].parent_path());
            if (!same(identity(previous[index]), expected[index]))
                throw ConflictError("目标在确认后发生变化");
            auto handle = std::make_unique<Handle>(CreateFileW(previous[index].c_str(),
                DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (handle->value == INVALID_HANDLE_VALUE) fail("无法锁定原目标");
            BY_HANDLE_FILE_INFORMATION current{};
            if (!GetFileInformationByHandle(handle->value, &current) ||
                !equalObject(current, expected[index].info)) throw ConflictError("目标身份已改变");
            const fs::path saved = backup / (std::to_wstring(index) + L".old");
            renameHandle(handle->value, saved);
            backups.push_back(saved);
            handles.push_back(std::move(handle));
        }
        for (const auto& item : outputs) {
            safeParents(item.first.parent_path());
            auto handle = std::make_unique<Handle>(CreateFileW(item.first.c_str(), DELETE | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            BY_HANDLE_FILE_INFORMATION information{};
            if (handle->value == INVALID_HANDLE_VALUE ||
                !GetFileInformationByHandle(handle->value, &information) ||
                (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                throw ArchiveError("无法锁定待提交文件");
            renameHandle(handle->value, item.second);
            committed.push_back(std::move(handle));
        }
    } catch (...) {
        bool restored = true;
        for (std::size_t index = committed.size(); index > 0; --index) {
            try { renameHandle(committed[index - 1]->value, outputs[index - 1].first); }
            catch (...) { restored = false; }
        }
        for (std::size_t index = 0; index < handles.size(); ++index) {
            try { renameHandle(handles[index]->value, previous[index]); }
            catch (...) { restored = false; }
        }
        if (!restored) {
            workspace.preserve = true;
            OperationSummary result;
            result.outcome = Outcome::Failed;
            result.recoveryPath = workspace.root.wstring();
            result.diagnostic = "提交或恢复失败，原文件备份保留于: " + utf8(result.recoveryPath);
            throw OperationError(std::move(result));
        }
        throw;
    }
    handles.clear();
    for (const auto& path : backups) workspace.adopt(path);
}

}

#include "ggzip/SecureDirectory.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/Diagnostics.h"
#include "ggzip/Text.h"
#include <windows.h>
#include <winternl.h>
#include <sddl.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <atomic>
#include <mutex>

namespace fs = std::filesystem;
namespace ggzip {
namespace {

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE input) : value(input) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); }
};

struct LocalMemory {
    void* value = nullptr;
    ~LocalMemory() { if (value) LocalFree(value); }
};

void fail(const char* message) {
    throw ArchiveError(std::string(message) + " (Windows " + std::to_string(GetLastError()) + ")");
}

std::wstring volumeOf(const fs::path& path) {
    std::vector<wchar_t> root(32768);
    if (!GetVolumePathNameW(win32Path(path).c_str(), root.data(), static_cast<DWORD>(root.size())))
        fail(diagnostic("无法确定目标文件系统"));
    return root.data();
}

std::wstring randomName(const std::wstring& prefix) {
    if (prefix.empty() || prefix.find_first_of(L"/\\:") != std::wstring::npos ||
        prefix.find(L'\0') != std::wstring::npos)
        throw ArchiveError(diagnostic("私有目录名称不安全"));
    unsigned char random[16]{};
    if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw ArchiveError(diagnostic("无法生成私有目录名称"));
    std::wostringstream name;
    name << prefix << std::hex << std::setfill(L'0');
    for (const auto byte : random) name << std::setw(2) << static_cast<unsigned int>(byte);
    return name.str();
}

}

std::wstring win32Path(const fs::path& path) {
    const auto absolute = fs::absolute(path).lexically_normal();
    auto value = fs::path(absolute).make_preferred().wstring();
    if (value.find(L'\0') != std::wstring::npos) throw ArchiveError(diagnostic("路径包含非法空字符"));
    if (value.rfind(L"\\\\?\\", 0) == 0) return value;
    if (value.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + value.substr(2);
    return L"\\\\?\\" + value;
}

FileSystemCapabilities fileSystemCapabilities(const fs::path& path) {
    const auto root = volumeOf(path);
    DWORD flags = 0;
    if (!GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, &flags, nullptr, 0))
        fail(diagnostic("无法读取目标文件系统能力"));
    FileSystemCapabilities result;
    result.volume = root;
    result.persistentAcls = (flags & FILE_PERSISTENT_ACLS) != 0;
    result.namedStreams = (flags & FILE_NAMED_STREAMS) != 0;
    return result;
}

bool sameVolume(const fs::path& first, const fs::path& second) {
    return _wcsicmp(volumeOf(first).c_str(), volumeOf(second).c_str()) == 0;
}

struct DirectoryLock::Impl {
    std::vector<std::unique_ptr<Handle>> handles;
    std::atomic_int persistentAcls{-1};
    fs::path path;
    std::mutex volumeMutex;
    std::wstring volumeId;
};

DirectoryLock::DirectoryLock(const fs::path& path, bool create) : impl_(std::make_unique<Impl>()) {
    auto spelling = fs::path(fs::absolute(path)).make_preferred().wstring();
    if (spelling.rfind(L"\\\\?\\UNC\\", 0) == 0) spelling = L"\\\\" + spelling.substr(8);
    else if (spelling.rfind(L"\\\\?\\", 0) == 0) spelling = spelling.substr(4);
    const auto absolute = fs::path(spelling).lexically_normal();
    impl_->path = absolute;
    fs::path current = absolute.root_path();
    auto open = [&](const fs::path& directory) {
        auto native = win32Path(directory);
        auto handle = std::make_unique<Handle>(CreateFileW(native.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (handle->value == INVALID_HANDLE_VALUE && create &&
            (GetLastError() == ERROR_PATH_NOT_FOUND || GetLastError() == ERROR_FILE_NOT_FOUND)) {
            if (!CreateDirectoryW(native.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
                fail(diagnostic("无法创建普通目录"));
            handle = std::make_unique<Handle>(CreateFileW(native.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        }
        BY_HANDLE_FILE_INFORMATION information{};
        if (handle->value == INVALID_HANDLE_VALUE ||
            !GetFileInformationByHandle(handle->value, &information) ||
            !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw ArchiveError(diagnostic("无法锁定普通目录: ") + utf8(directory.wstring()));
        impl_->handles.push_back(std::move(handle));
    };
    open(current);
    for (const auto& component : absolute.relative_path()) {
        current /= component;
        open(current);
    }
}

DirectoryLock::~DirectoryLock() = default;

std::wstring DirectoryLock::volume() const {
    if (!intact()) throw ArchiveError(diagnostic("目标目录发生不安全变化"));
    std::lock_guard guard(impl_->volumeMutex);
    if (!impl_->volumeId.empty()) return impl_->volumeId;
    std::vector<wchar_t> buffer(32768);
    const auto count = GetFinalPathNameByHandleW(impl_->handles.back()->value, buffer.data(),
        static_cast<DWORD>(buffer.size()), FILE_NAME_OPENED | VOLUME_NAME_GUID);
    if (count && count < buffer.size()) {
        const std::wstring name(buffer.data(), count);
        const auto separator = name.find(L"}\\");
        if (name.rfind(L"\\\\?\\Volume{", 0) == 0 && separator != std::wstring::npos) {
            impl_->volumeId = name.substr(0, separator + 2);
            return impl_->volumeId;
        }
    }
    return volumeOf(impl_->path);
}

bool DirectoryLock::intact() const {
    for (const auto& handle : impl_->handles) {
        BY_HANDLE_FILE_INFORMATION state{};
        if (!GetFileInformationByHandle(handle->value, &state) ||
            !(state.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (state.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    return true;
}

void DirectoryLock::requireWritable() const {
    if (!intact()) throw ArchiveError(diagnostic("目标目录状态发生不安全变化"));
    auto persistent = impl_->persistentAcls.load(std::memory_order_relaxed);
    if (persistent < 0) {
        DWORD flags = 0;
        if (!GetVolumeInformationByHandleW(impl_->handles.back()->value, nullptr, 0,
            nullptr, nullptr, &flags, nullptr, 0)) fail(diagnostic("无法读取目录文件系统能力"));
        persistent = (flags & FILE_PERSISTENT_ACLS) != 0;
        impl_->persistentAcls.store(persistent, std::memory_order_relaxed);
    }
    if (!persistent) return;
    LocalMemory security;
    const auto error = GetSecurityInfo(impl_->handles.back()->value, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, nullptr, &security.value);
    if (error != ERROR_SUCCESS) throw ArchiveError(diagnostic("无法读取目标目录写权限"));
    HANDLE tokenValue = nullptr;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_DUPLICATE, TRUE, &tokenValue)) {
        if (GetLastError() != ERROR_NO_TOKEN ||
            !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &tokenValue))
            fail(diagnostic("无法验证当前用户写权限"));
    }
    const Handle token(tokenValue);
    HANDLE duplicateValue = nullptr;
    if (!DuplicateToken(token.value, SecurityImpersonation, &duplicateValue)) fail(diagnostic("无法检查目录权限"));
    const Handle duplicate(duplicateValue);
    GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    std::vector<unsigned char> privileges(1024);
    DWORD privilegeBytes = static_cast<DWORD>(privileges.size());
    DWORD granted = 0;
    BOOL allowed = FALSE;
    if (!AccessCheck(security.value, duplicate.value, FILE_ADD_FILE, &mapping,
        reinterpret_cast<PPRIVILEGE_SET>(privileges.data()), &privilegeBytes, &granted, &allowed))
        fail(diagnostic("无法检查目录写权限"));
    if (!allowed) throw ArchiveError(diagnostic("目标目录不允许创建文件，原文件未移动"));
}

void DirectoryLock::renameFile(void* sourceHandle, const std::wstring& name) const {
    if (!intact()) throw ArchiveError(diagnostic("目标目录发生不安全变化"));
    if (name.empty() || name == L"." || name == L".." || name.find_first_of(L"/\\:") != std::wstring::npos ||
        name.find(L'\0') != std::wstring::npos) throw ArchiveError(diagnostic("事务文件名称不安全"));
    BY_HANDLE_FILE_INFORMATION directory{};
    if (!GetFileInformationByHandle(impl_->handles.back()->value, &directory) ||
        !(directory.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (directory.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw ArchiveError(diagnostic("目标目录发生不安全变化"));
    const auto size = sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t);
    std::vector<unsigned char> storage(size, 0);
    auto* information = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    information->RootDirectory = impl_->handles.back()->value;
    information->ReplaceIfExists = FALSE;
    information->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::copy(name.begin(), name.end(), information->FileName);
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto rename = reinterpret_cast<NTSTATUS (NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS)>(
        GetProcAddress(module, "NtSetInformationFile"));
    const auto errorCode = reinterpret_cast<ULONG (NTAPI*)(NTSTATUS)>(GetProcAddress(module, "RtlNtStatusToDosError"));
    if (!rename || !errorCode) throw ArchiveError(diagnostic("系统不支持安全的目录句柄提交"));
    IO_STATUS_BLOCK result{};
    const auto status = rename(sourceHandle, &result, information, static_cast<ULONG>(size),
        static_cast<FILE_INFORMATION_CLASS>(10));
    if (status < 0) {
        SetLastError(errorCode(status));
        fail(diagnostic("无法通过受保护目录提交文件"));
    }
}

fs::path createPrivateDirectory(const fs::path& parent, const std::wstring& prefix,
                               std::unique_ptr<DirectoryLock>& lock) {
    if (lock) throw ArchiveError(diagnostic("私有目录锁已被使用"));
    const auto name = randomName(prefix);
    DirectoryLock parents(parent);
    if (!fileSystemCapabilities(parent).persistentAcls)
        throw ArchiveError(diagnostic("文件系统不能保证私有暂存权限"));
    HANDLE tokenValue = nullptr;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &tokenValue)) {
        if (GetLastError() != ERROR_NO_TOKEN ||
            !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tokenValue)) fail(diagnostic("无法读取当前用户身份"));
    }
    const Handle token(tokenValue);
    DWORD required = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &required);
    std::vector<unsigned char> user(required);
    if (!required || !GetTokenInformation(token.value, TokenUser, user.data(), required, &required))
        fail(diagnostic("无法读取当前用户 SID"));
    const auto* information = reinterpret_cast<const TOKEN_USER*>(user.data());
    LocalMemory sid;
    if (!ConvertSidToStringSidW(information->User.Sid, reinterpret_cast<LPWSTR*>(&sid.value)))
        fail(diagnostic("无法转换当前用户身份"));
    const std::wstring descriptor = L"O:" + std::wstring(static_cast<wchar_t*>(sid.value)) +
        L"D:P(A;OICI;FA;;;" + static_cast<wchar_t*>(sid.value) + L")(A;OICI;FA;;;SY)";
    LocalMemory security;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(), SDDL_REVISION_1,
        &security.value, nullptr)) fail(diagnostic("无法创建私有目录权限"));
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), security.value, FALSE};
    const auto directory = fs::absolute(parent) / name;
    if (!CreateDirectoryW(win32Path(directory).c_str(), &attributes)) fail(diagnostic("无法创建私有操作目录"));
    auto guard = std::make_unique<DirectoryLock>(directory);
    try {
        const Handle handle(CreateFileW(win32Path(directory).c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        LocalMemory actual;
        PACL dacl = nullptr;
        PSID owner = nullptr;
        SECURITY_DESCRIPTOR_CONTROL control{};
        DWORD revision = 0;
        BY_HANDLE_FILE_INFORMATION file{};
        const DWORD error = GetSecurityInfo(handle.value, SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &dacl, nullptr, &actual.value);
        if (error != ERROR_SUCCESS || !GetFileInformationByHandle(handle.value, &file) ||
            (file.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !GetSecurityDescriptorControl(actual.value, &control, &revision) ||
            !(control & SE_DACL_PROTECTED) || !dacl || !owner || !EqualSid(owner, information->User.Sid))
            throw ArchiveError(diagnostic("文件系统不能保证私有暂存权限"));
        LocalMemory actualText;
        if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(actual.value, SDDL_REVISION_1,
            DACL_SECURITY_INFORMATION, reinterpret_cast<LPWSTR*>(&actualText.value), nullptr))
            fail(diagnostic("无法验证私有目录权限"));
        LocalMemory expectedText;
        if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(security.value, SDDL_REVISION_1,
            DACL_SECURITY_INFORMATION, reinterpret_cast<LPWSTR*>(&expectedText.value), nullptr) ||
            wcscmp(static_cast<wchar_t*>(actualText.value), static_cast<wchar_t*>(expectedText.value)) != 0)
            throw ArchiveError(diagnostic("私有目录权限验证不一致"));
    } catch (...) {
        guard.reset();
        RemoveDirectoryW(win32Path(directory).c_str());
        throw;
    }
    lock = std::move(guard);
    return directory;
}

fs::path createStagingDirectory(const fs::path& parent, const std::wstring& prefix,
                               std::unique_ptr<DirectoryLock>& lock) {
    DirectoryLock preferred(parent);
    if (fileSystemCapabilities(parent).persistentAcls)
        return createPrivateDirectory(parent, prefix, lock);
    PWSTR local = nullptr;
    const auto status = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local);
    fs::path fallback;
    if (SUCCEEDED(status) && local) fallback = local;
    if (local) CoTaskMemFree(local);
    if (fallback.empty() || !fileSystemCapabilities(fallback).persistentAcls)
        throw ArchiveError(diagnostic("没有支持受保护 ACL 的私有暂存位置"));
    return createPrivateDirectory(fallback, prefix, lock);
}

fs::path createPublicationDirectory(const fs::path& parent, const std::wstring& prefix,
                                   std::unique_ptr<DirectoryLock>& lock) {
    if (lock) throw ArchiveError(diagnostic("私有目录锁已被使用"));
    DirectoryLock parents(parent);
    if (fileSystemCapabilities(parent).persistentAcls)
        return createPrivateDirectory(parent, prefix, lock);
    const auto directory = fs::absolute(parent) / randomName(prefix);
    if (!CreateDirectoryW(win32Path(directory).c_str(), nullptr)) fail(diagnostic("无法创建目标卷提交目录"));
    try { lock = std::make_unique<DirectoryLock>(directory); }
    catch (...) { RemoveDirectoryW(win32Path(directory).c_str()); throw; }
    return directory;
}

}

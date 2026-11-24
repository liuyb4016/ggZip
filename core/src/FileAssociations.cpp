#include "ggzip/FileAssociations.h"
#include "ggzip/Diagnostics.h"
#include <windows.h>
#include <shlobj.h>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace ggzip {
namespace {

constexpr wchar_t kProgId[] = L"ggZip.Archive";
constexpr wchar_t kProgIdKey[] = L"Software\\Classes\\ggZip.Archive";
constexpr wchar_t kApplicationKey[] = L"Software\\Classes\\Applications\\ggzip_gui.exe";
constexpr wchar_t kCapabilitiesKey[] = L"Software\\ggZip\\Capabilities";
constexpr wchar_t kRegisteredApplications[] = L"Software\\RegisteredApplications";
constexpr std::array<const wchar_t*, 18> kExtensions = {
    L".zip", L".7z", L".rar", L".tar", L".gz", L".tgz",
    L".bz2", L".tbz", L".tbz2", L".xz", L".txz", L".zst",
    L".tzst", L".cab", L".iso", L".wim", L".jar", L".lzma"
};

struct RegistryKey {
    HKEY handle = nullptr;
    ~RegistryKey() { if (handle) RegCloseKey(handle); }
    RegistryKey() = default;
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
};

void requireSuccess(LSTATUS status) {
    if (status != ERROR_SUCCESS)
        throw std::runtime_error(diagnostic("文件关联注册表操作失败，Windows 错误码 ") +
                                 std::to_string(status));
}

void setValue(const std::wstring& path, const wchar_t* name, DWORD type,
              const BYTE* data, DWORD bytes) {
    RegistryKey key;
    requireSuccess(RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0,
                                  KEY_SET_VALUE, nullptr, &key.handle, nullptr));
    requireSuccess(RegSetValueExW(key.handle, name, 0, type, data, bytes));
}

void setString(const std::wstring& path, const wchar_t* name, const std::wstring& value) {
    setValue(path, name, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
             static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

std::wstring readString(const std::wstring& path, const wchar_t* name = nullptr) {
    RegistryKey key;
    const LSTATUS opened = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0,
                                       KEY_QUERY_VALUE, &key.handle);
    if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND) return {};
    requireSuccess(opened);
    DWORD bytes = 0;
    LSTATUS status = RegGetValueW(key.handle, nullptr, name, RRF_RT_REG_SZ,
                                 nullptr, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND) return {};
    requireSuccess(status);
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    requireSuccess(RegGetValueW(key.handle, nullptr, name, RRF_RT_REG_SZ,
                               nullptr, buffer.data(), &bytes));
    return buffer.data();
}

void deleteValue(const std::wstring& path, const wchar_t* name) {
    RegistryKey key;
    const LSTATUS opened = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0,
                                       KEY_SET_VALUE, &key.handle);
    if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND) return;
    requireSuccess(opened);
    const LSTATUS status = RegDeleteValueW(key.handle, name);
    if (status != ERROR_FILE_NOT_FOUND) requireSuccess(status);
}

void deleteTree(const std::wstring& path) {
    const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    if (status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND)
        requireSuccess(status);
}

std::wstring normalizedPath(const std::wstring& applicationPath, bool mustExist) {
    const std::filesystem::path path(applicationPath);
    if (applicationPath.empty() || applicationPath.find_first_of(L"\"\r\n") != std::wstring::npos ||
        !path.is_absolute() || (mustExist && !std::filesystem::is_regular_file(path)))
        throw std::runtime_error(diagnostic("文件关联需要有效的 GUI 程序绝对路径"));
    return path.lexically_normal().wstring();
}

std::wstring openCommand(const std::wstring& path) {
    return L"\"" + path + L"\" \"%1\"";
}

bool ownsCommand(const std::wstring& key, const std::wstring& command) {
    const std::wstring registered = readString(key + L"\\shell\\open\\command");
    return _wcsicmp(registered.c_str(), command.c_str()) == 0;
}

void notify(bool notifyShell) {
    if (notifyShell)
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

}

void registerFileAssociations(const std::wstring& applicationPath, bool notifyShell) {
    const std::wstring path = normalizedPath(applicationPath, true);
    const std::wstring command = openCommand(path);
    const std::wstring icon = L"\"" + path + L"\",0";
    setString(kProgIdKey, nullptr, L"ggZip 压缩包");
    setString(std::wstring(kProgIdKey) + L"\\DefaultIcon", nullptr, icon);
    setString(std::wstring(kProgIdKey) + L"\\shell\\open\\command", nullptr, command);
    setString(kApplicationKey, L"FriendlyAppName", L"ggZip");
    setString(std::wstring(kApplicationKey) + L"\\DefaultIcon", nullptr, icon);
    setString(std::wstring(kApplicationKey) + L"\\shell\\open\\command", nullptr, command);
    setString(kCapabilitiesKey, L"ApplicationName", L"ggZip");
    setString(kCapabilitiesKey, L"ApplicationDescription", L"ggZip 压缩包浏览、压缩与解压");
    setString(kCapabilitiesKey, L"ApplicationIcon", icon);
    for (const wchar_t* extension : kExtensions) {
        setValue(std::wstring(L"Software\\Classes\\") + extension + L"\\OpenWithProgids",
                 kProgId, REG_NONE, nullptr, 0);
        setString(std::wstring(kApplicationKey) + L"\\SupportedTypes", extension, L"");
        setString(std::wstring(kCapabilitiesKey) + L"\\FileAssociations", extension, kProgId);
    }
    setString(kRegisteredApplications, L"ggZip", kCapabilitiesKey);
    notify(notifyShell);
}

void unregisterFileAssociations(const std::wstring& applicationPath, bool notifyShell) {
    const std::wstring command = openCommand(normalizedPath(applicationPath, false));
    if (!ownsCommand(kProgIdKey, command)) return;
    for (const wchar_t* extension : kExtensions) {
        deleteValue(std::wstring(L"Software\\Classes\\") + extension + L"\\OpenWithProgids",
                    kProgId);
    }
    if (readString(kRegisteredApplications, L"ggZip") == kCapabilitiesKey)
        deleteValue(kRegisteredApplications, L"ggZip");
    deleteTree(kCapabilitiesKey);
    if (ownsCommand(kApplicationKey, command)) deleteTree(kApplicationKey);
    deleteTree(kProgIdKey);
    notify(notifyShell);
}

}

#include "RegistryUtil.h"
#include "Common.h"

namespace ggshell {

namespace {

constexpr wchar_t kClsidKey[] = L"Software\\Classes\\CLSID\\";

bool setValue(const wchar_t* subkey, const wchar_t* name, const wchar_t* value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return false;
    const bool ok = RegSetValueExW(key, name, 0, REG_SZ,
                                   reinterpret_cast<const BYTE*>(value),
                                   static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)))
                    == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

} // namespace

bool registerServer() {
    wchar_t dll[MAX_PATH * 2]{};
    GetModuleFileNameW(g_hInstance, dll, MAX_PATH * 2);

    const std::wstring clsidKey = kClsidKey + std::wstring(clsidString());
    bool ok = setValue(clsidKey.c_str(), nullptr, L"ggZip 压缩右键菜单");
    ok = setValue((clsidKey + L"\\InprocServer32").c_str(), nullptr, dll) && ok;
    ok = setValue((clsidKey + L"\\InprocServer32").c_str(), L"ThreadingModel",
                  L"Apartment") && ok;

    // 文件（任意类型）、文件夹、文件夹空白处背景
    const wchar_t* bases[] = { L"*", L"Directory", L"Directory\\Background" };
    for (const wchar_t* base : bases) {
        std::wstring key = std::wstring(L"Software\\Classes\\") + base +
                           L"\\shellex\\ContextMenuHandlers\\ggZip";
        ok = setValue(key.c_str(), nullptr, clsidString()) && ok;
    }
    return ok;
}

bool unregisterServer() {
    const std::wstring clsidKey = std::wstring(kClsidKey) + clsidString();
    SHDeleteKeyW(HKEY_CURRENT_USER, clsidKey.c_str());
    const wchar_t* bases[] = { L"*", L"Directory", L"Directory\\Background" };
    for (const wchar_t* base : bases) {
        std::wstring key = std::wstring(L"Software\\Classes\\") + base +
                           L"\\shellex\\ContextMenuHandlers\\ggZip";
        SHDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
    }
    return true;
}

} // namespace ggshell

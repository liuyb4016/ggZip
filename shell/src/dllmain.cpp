// ggZip Shell 扩展 —— DLL 入口与四个标准导出
#include <initguid.h>   // 让 CLSID_ggZipMenu 在本编译单元成为定义
#include <olectl.h>     // SELFREG_E_CLASS
#include "Common.h"
#include "ClassFactory.h"
#include "RegistryUtil.h"

namespace ggshell {
HINSTANCE g_hInstance = nullptr;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        ggshell::g_hInstance = hInst;
        DisableThreadLibraryCalls(hInst);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;
    if (IsEqualCLSID(rclsid, ggshell::CLSID_ggZipExplorerCommand))
        return ggshell::ClassFactory::create(riid, ppv, true);
    if (!IsEqualCLSID(rclsid, ggshell::CLSID_ggZipMenu)) return CLASS_E_CLASSNOTAVAILABLE;
    return ggshell::ClassFactory::create(riid, ppv);
}

STDAPI DllCanUnloadNow() {
    return ggshell::ClassFactory::canUnload() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    return ggshell::registerServer() ? S_OK : SELFREG_E_CLASS;
}

STDAPI DllUnregisterServer() {
    return ggshell::unregisterServer() ? S_OK : SELFREG_E_CLASS;
}

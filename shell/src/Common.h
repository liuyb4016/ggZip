// ggZip Shell 扩展 —— 公共定义
// 独立的 C++ COM DLL（不依赖 Qt/core），由 Explorer 加载，只做两件事：
// 往右键菜单插入 ggZip 子菜单；点击后启动 ggz.exe 干活。
#pragma once

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include <vector>

namespace ggshell {

// 模块句柄（DllMain 里赋值，注册/资源都要用）
extern HINSTANCE g_hInstance;

// {28250246-8FC5-415D-95E3-6102C059ECEF}
DEFINE_GUID(CLSID_ggZipMenu,
    0x28250246, 0x8fc5, 0x415d, 0x95, 0xe3, 0x61, 0x02, 0xc0, 0x59, 0xec, 0xef);

inline const wchar_t* clsidString() {
    return L"{28250246-8FC5-415D-95E3-6102C059ECEF}";
}

DEFINE_GUID(CLSID_ggZipExplorerCommand,
    0x9d0f49ad, 0x69c1, 0x43c3, 0xa7, 0x12, 0xf3, 0xcb, 0x64, 0xf6, 0x92, 0x10);

// 存活 COM 对象 RAII 计数：DllCanUnloadNow 依据它判断能否卸载
struct LiveObjectGuard {
    LiveObjectGuard();
    ~LiveObjectGuard();
};

} // namespace ggshell

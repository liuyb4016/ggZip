// ggZip Shell 扩展 —— 右键菜单 COM 对象（IShellExtInit + IContextMenu）
#pragma once

#include "Common.h"

namespace ggshell {

class ShellMenu : public IShellExtInit, public IContextMenu {
public:
    ShellMenu();
    ~ShellMenu();   // COM 接口无虚析构，不能用 override

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IShellExtInit：Explorer 提供选中项
    STDMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject* pDataObj,
                            HKEY hkeyProgID) override;

    // IContextMenu
    STDMETHODIMP QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst,
                                  UINT idCmdLast, UINT uFlags) override;
    STDMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO pici) override;
    STDMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uType, UINT* pwReserved,
                                  CHAR* pszName, UINT cchMax) override;

private:
    enum class Action { SmartExtract, ExtractHere, ExtractToSub, Create7z, CreateZip };
    struct Item {
        Action action;
        std::wstring verb;   // 程序化调用名，如 "ggzip.smart"
        std::wstring help;   // 状态栏提示
    };

    void classify();         // 把 m_paths 分成压缩包 / 普通项，预算压缩目标名
    void dispatch(Action a); // 组装 ggz 命令并交给启动器

    LONG m_ref = 1;
    LiveObjectGuard m_live;
    UINT m_idFirst = 0;                    // 本次 QueryContextMenu 的起始 id
    std::vector<std::wstring> m_paths;     // 选中的完整路径
    std::wstring m_backgroundDir;          // 目录背景模式（空 = 文件选择模式）
    std::vector<std::wstring> m_archives;  // 选中的压缩包
    bool m_hasOthers = false;              // 选中项里含非压缩包
    std::wstring m_createBase;             // 压缩目标主名（无扩展名）
    std::wstring m_createDir;              // 压缩目标所在目录
    std::vector<Item> m_items;             // 与子菜单位置一一对应
};

} // namespace ggshell

#include "ShellMenu.h"
#include "ArchDetect.h"
#include "Launcher.h"

#include <algorithm>
#include <cwchar>

namespace ggshell {

namespace {

std::wstring parentOf(const std::wstring& p) {
    const size_t pos = p.find_last_of(L'\\');
    return pos == std::wstring::npos ? std::wstring() : p.substr(0, pos);
}

std::wstring nameOf(const std::wstring& p) {
    return PathFindFileNameW(p.c_str());
}

} // namespace

ShellMenu::ShellMenu() = default;

ShellMenu::~ShellMenu() = default;

STDMETHODIMP ShellMenu::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown)) *ppv = static_cast<IShellExtInit*>(this);   // 主基类，消歧
    else if (IsEqualIID(riid, IID_IShellExtInit)) *ppv = static_cast<IShellExtInit*>(this);
    else if (IsEqualIID(riid, IID_IContextMenu)) *ppv = static_cast<IContextMenu*>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) ShellMenu::AddRef() { return ++m_ref; }

STDMETHODIMP_(ULONG) ShellMenu::Release() {
    const ULONG r = --m_ref;
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP ShellMenu::Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject* pDataObj, HKEY) {
    if (pidlFolder != nullptr) {
        m_paths.clear();
        m_backgroundDir.clear();
        wchar_t dir[MAX_PATH];
        if (SHGetPathFromIDListW(pidlFolder, dir)) m_backgroundDir = dir;
        return S_OK;
    }
    if (pDataObj == nullptr) return E_INVALIDARG;
    m_backgroundDir.clear();

    FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg{};
    if (FAILED(pDataObj->GetData(&fmt, &stg))) return E_INVALIDARG;
    const HDROP hdrop = static_cast<HDROP>(stg.hGlobal);
    const UINT n = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
    m_paths.clear();
    for (UINT i = 0; i < n; ++i) {
        const UINT len = DragQueryFileW(hdrop, i, nullptr, 0) + 1;
        std::wstring buf(len, L'\0');
        DragQueryFileW(hdrop, i, buf.data(), len);
        buf.resize(wcslen(buf.c_str()));
        m_paths.push_back(buf);
    }
    ReleaseStgMedium(&stg);
    return m_paths.empty() ? E_INVALIDARG : S_OK;
}

void ShellMenu::classify() {
    m_archives.clear();
    m_hasOthers = false;
    m_createBase.clear();
    m_createDir.clear();
    for (const auto& p : m_paths) {
        if (isArchive(p)) m_archives.push_back(p);
        else m_hasOthers = true;
    }
    // 压缩组：目录背景，或选中了非压缩包时才有意义（纯压缩包选择不再套娃）
    if (!m_backgroundDir.empty()) {
        m_createDir = m_backgroundDir;
        m_createBase = stemOf(m_backgroundDir);
    } else if (m_hasOthers && !m_paths.empty()) {
        m_createDir = parentOf(m_paths[0]);
        m_createBase = stemOf(m_paths[0]);
    }
}

STDMETHODIMP ShellMenu::QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst,
                                         UINT idCmdLast, UINT uFlags) {
    if (uFlags & CMF_DEFAULTONLY) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

    m_items.clear();
    m_idFirst = idCmdFirst;
    classify();

    struct Entry { Action a; std::wstring label; std::wstring verb; std::wstring help; };
    std::vector<Entry> entries;
    if (!m_archives.empty()) {
        entries.push_back({ Action::SmartExtract, L"智能解压(&S)", L"ggzip.smart",
                            L"自动决定解压到当前文件夹还是子文件夹" });
        entries.push_back({ Action::ExtractHere, L"解压到当前文件夹(&H)", L"ggzip.here",
                            L"把选中的压缩包解压到所在文件夹" });
        if (m_archives.size() == 1 && !m_hasOthers) {
            entries.push_back({ Action::ExtractToSub,
                                L"解压到 \"" + stemOf(m_archives[0]) + L"\\\"(&E)", L"ggzip.sub",
                                L"解压到同名子文件夹" });
        }
    }
    if (!m_backgroundDir.empty() || m_hasOthers) {
        entries.push_back({ Action::Create7z,
                            L"添加到 \"" + m_createBase + L".7z\"(&7)", L"ggzip.7z",
                            L"把选中项压缩为一个 7z 压缩包" });
        entries.push_back({ Action::CreateZip,
                            L"添加到 \"" + m_createBase + L".zip\"(&Z)", L"ggzip.zip",
                            L"把选中项压缩为一个 zip 压缩包" });
    }
    if (entries.empty()) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    if (idCmdFirst + entries.size() > idCmdLast) return MAKE_HRESULT(SEVERITY_ERROR, 0, 0);

    HMENU sub = CreatePopupMenu();
    if (sub == nullptr) return E_OUTOFMEMORY;
    UINT id = idCmdFirst;
    for (UINT i = 0; i < entries.size(); ++i) {
        InsertMenuW(sub, i, MF_BYPOSITION | MF_STRING, id, entries[i].label.c_str());
        m_items.push_back({ entries[i].a, entries[i].verb, entries[i].help });
        ++id;
    }
    // 单个压缩包时把智能解压设为默认项（加粗，双击/回车触发）
    if (m_archives.size() == 1 && !m_hasOthers && m_backgroundDir.empty())
        SetMenuDefaultItem(sub, 0, TRUE);
    if (!InsertMenuW(hmenu, indexMenu, MF_BYPOSITION | MF_POPUP,
                     reinterpret_cast<UINT_PTR>(sub), L"ggZip")) {
        DestroyMenu(sub);
        return E_FAIL;
    }
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, id - idCmdFirst);
}

STDMETHODIMP ShellMenu::InvokeCommand(LPCMINVOKECOMMANDINFO pici) {
    if (pici == nullptr) return E_POINTER;
    const bool unicode = pici->cbSize >= sizeof(CMINVOKECOMMANDINFOEX) &&
                         (pici->fMask & CMIC_MASK_UNICODE) != 0;
    int index = -1;
    if (unicode) {
        const wchar_t* verbW = reinterpret_cast<LPCMINVOKECOMMANDINFOEX>(pici)->lpVerbW;
        if (verbW != nullptr && HIWORD(verbW) != 0) {
            for (UINT i = 0; i < m_items.size(); ++i)
                if (m_items[i].verb == verbW) { index = static_cast<int>(i); break; }
        }
    }
    if (index < 0 && HIWORD(pici->lpVerb) == 0) {
        const UINT offset = LOWORD(reinterpret_cast<UINT_PTR>(pici->lpVerb)) - m_idFirst;
        if (offset < m_items.size()) index = static_cast<int>(offset);
    }
    if (index < 0 && HIWORD(pici->lpVerb) != 0) {
        wchar_t wverb[64];
        if (MultiByteToWideChar(CP_ACP, 0, pici->lpVerb, -1, wverb, 64) > 0) {
            for (UINT i = 0; i < m_items.size(); ++i)
                if (m_items[i].verb == wverb) { index = static_cast<int>(i); break; }
        }
    }
    if (index < 0) return E_INVALIDARG;
    dispatch(m_items[index].action);
    return S_OK;
}

STDMETHODIMP ShellMenu::GetCommandString(UINT_PTR idCmd, UINT uType, UINT*,
                                         CHAR* pszName, UINT cchMax) {
    if (idCmd >= m_items.size() || pszName == nullptr || cchMax == 0) return E_INVALIDARG;
    // GCS_UNICODE 是位标志(0x4)，先剥离再区分动词/帮助文本
    const bool wantVerb = (uType & ~GCS_UNICODE) == GCS_VERB;
    const std::wstring& s = wantVerb ? m_items[idCmd].verb : m_items[idCmd].help;
    if (uType & GCS_UNICODE) {
        wcsncpy_s(reinterpret_cast<wchar_t*>(pszName), cchMax, s.c_str(), _TRUNCATE);
    } else {
        char buf[256];
        const int n = WideCharToMultiByte(CP_ACP, 0, s.c_str(), -1, buf, sizeof(buf),
                                          nullptr, nullptr);
        if (n <= 0) return E_FAIL;
        strncpy_s(pszName, cchMax, buf, _TRUNCATE);
    }
    return S_OK;
}

void ShellMenu::dispatch(Action a) {
    const std::wstring exe = findGgz();
    std::vector<std::vector<std::wstring>> commands;
    switch (a) {
    case Action::SmartExtract:
        for (const auto& p : m_archives)
            commands.push_back({ L"smart-extract", p, L"-o", parentOf(p) });
        break;
    case Action::ExtractHere:
        for (const auto& p : m_archives)
            commands.push_back({ L"extract", p, L"-o", parentOf(p) });
        break;
    case Action::ExtractToSub: {
        const std::wstring& p = m_archives[0];
        commands.push_back({ L"extract", p, L"-o", parentOf(p) + L"\\" + stemOf(p) });
        break;
    }
    case Action::Create7z:
    case Action::CreateZip: {
        const wchar_t* ext = (a == Action::Create7z) ? L".7z" : L".zip";
        std::vector<std::wstring> cmd = { L"create", m_createDir + L"\\" + m_createBase + ext };
        cmd.insert(cmd.end(), m_paths.begin(), m_paths.end());
        commands.push_back(std::move(cmd));
        break;
    }
    }
    launchAsync(exe, commands);
}

} // namespace ggshell

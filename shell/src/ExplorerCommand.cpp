#include "ExplorerCommand.h"
#include "ArchDetect.h"
#include "Launcher.h"
#include <algorithm>
#include <filesystem>
#include <new>

namespace fs = std::filesystem;
namespace ggshell {
namespace {

HRESULT pathsOf(IShellItemArray* items, std::vector<std::wstring>& paths) {
    if (!items) return E_INVALIDARG;
    DWORD count = 0;
    HRESULT result = items->GetCount(&count);
    if (FAILED(result)) return result;
    for (DWORD index = 0; index < count; ++index) {
        IShellItem* item = nullptr;
        result = items->GetItemAt(index, &item);
        if (FAILED(result)) return result;
        PWSTR name = nullptr;
        result = item->GetDisplayName(SIGDN_FILESYSPATH, &name);
        item->Release();
        if (FAILED(result)) return result;
        try { paths.emplace_back(name); } catch (...) { CoTaskMemFree(name); throw; }
        CoTaskMemFree(name);
    }
    return paths.empty() ? E_INVALIDARG : S_OK;
}

bool chinese() {
    wchar_t language[32]{};
    DWORD bytes = sizeof(language);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ggZip\\ggZip\\ui", L"language",
        RRF_RT_REG_SZ, nullptr, language, &bytes) == ERROR_SUCCESS) return wcscmp(language, L"zh_CN") == 0;
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
}

class Commands : public IEnumExplorerCommand {
public:
    explicit Commands(ULONG cursor = 0) : cursor_(cursor) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != IID_IUnknown && iid != IID_IEnumExplorerCommand) return E_NOINTERFACE;
        *output = static_cast<IEnumExplorerCommand*>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override {
        const auto remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
    STDMETHODIMP Next(ULONG count, IExplorerCommand** output, ULONG* fetched) override {
        if (!output || (!fetched && count != 1)) return E_POINTER;
        if (fetched) *fetched = 0;
        ULONG actual = 0;
        while (actual < count && cursor_ < 5) {
            output[actual] = new (std::nothrow) ExplorerCommand(cursor_ + 1);
            if (!output[actual]) {
                for (ULONG index = 0; index < actual; ++index) { output[index]->Release(); output[index] = nullptr; }
                return E_OUTOFMEMORY;
            }
            ++actual;
            ++cursor_;
        }
        if (fetched) *fetched = actual;
        return actual == count ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG count) override {
        const auto skipped = std::min(count, 5 - cursor_);
        cursor_ += skipped;
        return skipped == count ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override { cursor_ = 0; return S_OK; }
    STDMETHODIMP Clone(IEnumExplorerCommand** output) override {
        if (!output) return E_POINTER;
        *output = new (std::nothrow) Commands(cursor_);
        return *output ? S_OK : E_OUTOFMEMORY;
    }
private:
    std::atomic_ulong references_{1};
    ULONG cursor_;
    LiveObjectGuard alive_;
};

}

ExplorerCommand::ExplorerCommand(unsigned action) : action_(action) {}
ExplorerCommand::~ExplorerCommand() { if (site_) site_->Release(); }
STDMETHODIMP ExplorerCommand::QueryInterface(REFIID iid, void** output) {
    if (!output) return E_POINTER;
    *output = nullptr;
    if (iid == IID_IUnknown || iid == IID_IExplorerCommand) *output = static_cast<IExplorerCommand*>(this);
    else if (iid == IID_IObjectWithSite) *output = static_cast<IObjectWithSite*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}
STDMETHODIMP_(ULONG) ExplorerCommand::AddRef() { return ++references_; }
STDMETHODIMP_(ULONG) ExplorerCommand::Release() {
    const auto remaining = --references_;
    if (!remaining) delete this;
    return remaining;
}
STDMETHODIMP ExplorerCommand::GetTitle(IShellItemArray*, PWSTR* title) {
    if (!title) return E_POINTER;
    static const wchar_t* english[]{L"ggZip", L"Smart Extract", L"Extract Here", L"Extract to New Folder",
                                   L"Create 7z Archive", L"Create ZIP Archive"};
    static const wchar_t* chineseTitles[]{L"ggZip", L"智能解压", L"解压到当前目录", L"解压到新目录",
                                         L"创建 7z 压缩包", L"创建 ZIP 压缩包"};
    return SHStrDupW((chinese() ? chineseTitles : english)[action_], title);
}
STDMETHODIMP ExplorerCommand::GetIcon(IShellItemArray*, PWSTR* icon) {
    if (!icon) return E_POINTER;
    *icon = nullptr;
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(g_hInstance, path, 32768)) return E_FAIL;
    return SHStrDupW((std::wstring(path) + L",0").c_str(), icon);
}
STDMETHODIMP ExplorerCommand::GetToolTip(IShellItemArray*, PWSTR* tooltip) {
    if (!tooltip) return E_POINTER;
    *tooltip = nullptr;
    return E_NOTIMPL;
}
STDMETHODIMP ExplorerCommand::GetCanonicalName(GUID* name) {
    if (!name) return E_POINTER;
    *name = CLSID_ggZipExplorerCommand;
    name->Data1 += action_;
    return S_OK;
}
STDMETHODIMP ExplorerCommand::GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) {
    if (!state) return E_POINTER;
    *state = ECS_HIDDEN;
    try {
        std::vector<std::wstring> paths;
        if (FAILED(pathsOf(items, paths))) return S_OK;
        const bool archives = std::all_of(paths.begin(), paths.end(), [](const auto& path) { return isArchive(path); });
        if (action_ == 0 || (action_ <= 3 ? archives : !archives)) *state = ECS_ENABLED;
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
STDMETHODIMP ExplorerCommand::Invoke(IShellItemArray* items, IBindCtx*) {
    try {
        std::vector<std::wstring> paths;
        if (FAILED(pathsOf(items, paths)) || action_ == 0) return E_INVALIDARG;
        EXPCMDSTATE state;
        GetState(items, TRUE, &state);
        if (state != ECS_ENABLED) return E_INVALIDARG;
        std::vector<std::vector<std::wstring>> commands;
        if (action_ <= 3) {
            if (paths.size() > 1) {
                std::vector<std::wstring> batch{L"batch-extract"};
                batch.insert(batch.end(), paths.begin(), paths.end());
                commands.push_back(std::move(batch));
            } else {
                const fs::path source(paths.front());
                fs::path target = source.parent_path();
                if (action_ == 3) target /= source.stem();
                commands.push_back({action_ == 1 ? L"smart-extract" : L"extract", source.wstring(), L"-o", target.wstring()});
            }
        } else {
            const fs::path source(paths.front());
            const auto target = source.parent_path() / (source.stem().wstring() + (action_ == 4 ? L".7z" : L".zip"));
            std::vector<std::wstring> create{L"create", target.wstring()};
            create.insert(create.end(), paths.begin(), paths.end());
            commands.push_back(std::move(create));
        }
        launchAsync(findGgz(), commands);
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
STDMETHODIMP ExplorerCommand::GetFlags(EXPCMDFLAGS* flags) {
    if (!flags) return E_POINTER;
    *flags = action_ == 0 ? ECF_HASSUBCOMMANDS : ECF_DEFAULT;
    return S_OK;
}
STDMETHODIMP ExplorerCommand::EnumSubCommands(IEnumExplorerCommand** commands) {
    if (!commands) return E_POINTER;
    *commands = nullptr;
    if (action_ != 0) return E_NOTIMPL;
    *commands = new (std::nothrow) Commands();
    return *commands ? S_OK : E_OUTOFMEMORY;
}
STDMETHODIMP ExplorerCommand::SetSite(IUnknown* site) {
    if (site) site->AddRef();
    if (site_) site_->Release();
    site_ = site;
    return S_OK;
}
STDMETHODIMP ExplorerCommand::GetSite(REFIID iid, void** site) {
    if (!site) return E_POINTER;
    *site = nullptr;
    return site_ ? site_->QueryInterface(iid, site) : E_FAIL;
}

}

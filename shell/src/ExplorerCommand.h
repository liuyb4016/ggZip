#pragma once
#include "Common.h"
#include <atomic>

namespace ggshell {

class ExplorerCommand : public IExplorerCommand, public IObjectWithSite {
public:
    explicit ExplorerCommand(unsigned action = 0);
    STDMETHODIMP QueryInterface(REFIID iid, void** output) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP GetTitle(IShellItemArray* items, PWSTR* title) override;
    STDMETHODIMP GetIcon(IShellItemArray* items, PWSTR* icon) override;
    STDMETHODIMP GetToolTip(IShellItemArray* items, PWSTR* tooltip) override;
    STDMETHODIMP GetCanonicalName(GUID* name) override;
    STDMETHODIMP GetState(IShellItemArray* items, BOOL slow, EXPCMDSTATE* state) override;
    STDMETHODIMP Invoke(IShellItemArray* items, IBindCtx* context) override;
    STDMETHODIMP GetFlags(EXPCMDFLAGS* flags) override;
    STDMETHODIMP EnumSubCommands(IEnumExplorerCommand** commands) override;
    STDMETHODIMP SetSite(IUnknown* site) override;
    STDMETHODIMP GetSite(REFIID iid, void** site) override;
private:
    ~ExplorerCommand();
    std::atomic_ulong references_{1};
    unsigned action_;
    IUnknown* site_ = nullptr;
    LiveObjectGuard alive_;
};

}

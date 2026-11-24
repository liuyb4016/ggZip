#include "ClassFactory.h"
#include "ShellMenu.h"
#include "ExplorerCommand.h"

#include <atomic>

namespace ggshell {

LONG ClassFactory::s_locks = 0;

// 全库存活对象计数（ClassFactory + ShellMenu），供 DllCanUnloadNow 使用
static std::atomic_long& liveObjects() {
    static std::atomic_long n{ 0 };
    return n;
}

LiveObjectGuard::LiveObjectGuard()  { ++liveObjects(); }
LiveObjectGuard::~LiveObjectGuard() { --liveObjects(); }

HRESULT ClassFactory::create(REFIID riid, void** ppv, bool modern) {
    auto* f = new (std::nothrow) ClassFactory();
    if (f == nullptr) return E_OUTOFMEMORY;
    f->modern_ = modern;
    const HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

bool ClassFactory::canUnload() {
    return liveObjects().load() == 0 && InterlockedCompareExchange(&s_locks, 0, 0) == 0;
}

STDMETHODIMP ClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
        *ppv = static_cast<IClassFactory*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) ClassFactory::AddRef() { return ++m_ref; }

STDMETHODIMP_(ULONG) ClassFactory::Release() {
    const ULONG r = --m_ref;
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP ClassFactory::CreateInstance(IUnknown* punkOuter, REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;
    if (punkOuter != nullptr) return CLASS_E_NOAGGREGATION;
    if (modern_) {
        auto* command = new (std::nothrow) ExplorerCommand();
        if (!command) return E_OUTOFMEMORY;
        const auto result = command->QueryInterface(riid, ppv);
        command->Release();
        return result;
    }
    auto* menu = new (std::nothrow) ShellMenu();
    if (menu == nullptr) return E_OUTOFMEMORY;
    const HRESULT hr = menu->QueryInterface(riid, ppv);
    menu->Release();   // QueryInterface 成功时内部已 AddRef
    return hr;
}

STDMETHODIMP ClassFactory::LockServer(BOOL fLock) {
    if (fLock) InterlockedIncrement(&s_locks); else InterlockedDecrement(&s_locks);
    return S_OK;
}

} // namespace ggshell

#include "ClassFactory.h"
#include "ShellMenu.h"

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

HRESULT ClassFactory::create(REFIID riid, void** ppv) {
    auto* f = new (std::nothrow) ClassFactory();
    if (f == nullptr) return E_OUTOFMEMORY;
    const HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

bool ClassFactory::canUnload() {
    return liveObjects().load() == 0 && s_locks == 0;
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
    auto* menu = new (std::nothrow) ShellMenu();
    if (menu == nullptr) return E_OUTOFMEMORY;
    const HRESULT hr = menu->QueryInterface(riid, ppv);
    menu->Release();   // QueryInterface 成功时内部已 AddRef
    return hr;
}

STDMETHODIMP ClassFactory::LockServer(BOOL fLock) {
    if (fLock) ++s_locks; else --s_locks;
    return S_OK;
}

} // namespace ggshell

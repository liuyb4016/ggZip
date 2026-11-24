// ggZip Shell 扩展 —— 类工厂
#pragma once

#include "Common.h"

namespace ggshell {

class ShellMenu;

class ClassFactory : public IClassFactory {
public:
    static HRESULT create(REFIID riid, void** ppv, bool modern = false);
    static bool canUnload();   // 无存活对象且服务器锁为 0 时允许卸载

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown* punkOuter, REFIID riid, void** ppv) override;
    STDMETHODIMP LockServer(BOOL fLock) override;

private:
    ClassFactory() = default;
    ~ClassFactory() = default;
    static LONG s_locks;       // LockServer 引用（服务器级锁）
    LONG m_ref = 1;
    bool modern_ = false;
    LiveObjectGuard alive_;
};

} // namespace ggshell

// shell_host_test —— 不经 Explorer 测试 ggZip Shell 扩展
// 用法: shell_host_test <ggzip_shell.dll 完整路径> [ggz.exe 路径（可为假路径）]
// 覆盖: regsvr32 注册(HKCU) → CoCreateInstance → 四种选中场景的菜单断言 →
//       GGZIP_SHELL_DRYRUN 干跑断言命令行 → GetCommandString → 注销验证
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static const CLSID kClsid = {
    0x28250246, 0x8fc5, 0x415d, { 0x95, 0xe3, 0x61, 0x02, 0xc0, 0x59, 0xec, 0xef }
};

static int g_failed = 0;

static void check(bool ok, const std::string& msg) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", msg.c_str());
    if (!ok) ++g_failed;
}

// ---------- 最小 IDataObject：只支持 CF_HDROP ----------
class TestDataObject : public IDataObject {
public:
    explicit TestDataObject(HGLOBAL h) : m_h(h) {}
    ~TestDataObject() { if (m_h) GlobalFree(m_h); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDataObject))
            *ppv = static_cast<IDataObject*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        ++m_ref;
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP GetData(FORMATETC* f, STGMEDIUM* stg) override {
        if (f == nullptr || stg == nullptr) return E_POINTER;
        if (f->cfFormat != CF_HDROP || !(f->tymed & TYMED_HGLOBAL)) return DV_E_FORMATETC;
        // 复制一份：调用方 ReleaseStgMedium 时会 GlobalFree
        const SIZE_T size = GlobalSize(m_h);
        HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size);
        memcpy(GlobalLock(copy), GlobalLock(m_h), size);
        GlobalUnlock(copy);
        GlobalUnlock(m_h);
        stg->tymed = TYMED_HGLOBAL;
        stg->hGlobal = copy;
        stg->pUnkForRelease = nullptr;
        return S_OK;
    }
    STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    STDMETHODIMP QueryGetData(FORMATETC* f) override {
        if (f == nullptr) return E_POINTER;
        return f->cfFormat == CF_HDROP ? S_OK : DV_E_FORMATETC;
    }
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
        if (out) out->ptd = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    STDMETHODIMP DUnadvise(DWORD) override { return OLE_E_NOCONNECTION; }
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    HGLOBAL m_h;
    ULONG m_ref = 1;
};

// ---------- 测试辅助 ----------

static HGLOBAL makeHDrop(const std::vector<std::wstring>& paths) {
    size_t chars = 2;   // 每个路径一个终止 0 + 末尾多一个 0
    for (const auto& p : paths) chars += p.size() + 1;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                            sizeof(DROPFILES) + chars * sizeof(wchar_t));
    auto* df = static_cast<DROPFILES*>(GlobalLock(g));
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    auto* w = reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(df) + sizeof(DROPFILES));
    for (const auto& p : paths) {
        wcscpy(w, p.c_str());
        w += p.size() + 1;
    }
    GlobalUnlock(g);
    return g;
}

static void writeDummy(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (f) { fwrite("ggzip test\n", 1, 11, f); fclose(f); }
}

static int runRegsvr32(const std::wstring& args) {
    return _wsystem((L"regsvr32 " + args).c_str());
}

static IContextMenu* createMenu(bool expectOk) {
    IContextMenu* m = nullptr;
    const HRESULT hr = CoCreateInstance(kClsid, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_IContextMenu, reinterpret_cast<void**>(&m));
    check(expectOk ? (SUCCEEDED(hr) && m != nullptr) : FAILED(hr),
          expectOk ? "CoCreateInstance(CLSID_ggZipMenu) 成功"
                   : "注销后 CoCreateInstance 应失败");
    return m;
}

struct MenuResult {
    UINT used = 0;
    std::vector<std::wstring> labels;
};

static MenuResult queryMenu(IContextMenu* m, IDataObject* obj, PCIDLIST_ABSOLUTE pidl) {
    MenuResult r;
    if (m == nullptr) return r;
    // Initialize 在 IShellExtInit 接口上，需先 QI
    IShellExtInit* init = nullptr;
    if (FAILED(m->QueryInterface(IID_IShellExtInit, reinterpret_cast<void**>(&init))))
        return r;
    const HRESULT ihr = init->Initialize(pidl, obj, nullptr);
    init->Release();
    if (FAILED(ihr)) return r;
    HMENU h = CreatePopupMenu();
    const HRESULT hr = m->QueryContextMenu(h, 0, 1, 10000, CMF_NORMAL);
    r.used = SUCCEEDED(hr) ? LOWORD(hr) : 0;
    HMENU sub = GetSubMenu(h, 0);
    for (UINT i = 0; i < r.used; ++i) {
        wchar_t buf[256]{};
        GetMenuStringW(sub, i, buf, 256, MF_BYPOSITION);
        r.labels.push_back(buf);
    }
    DestroyMenu(h);
    return r;
}

static bool labelsContain(const MenuResult& r, const wchar_t* text) {
    for (const auto& l : r.labels)
        if (l.find(text) != std::wstring::npos) return true;
    return false;
}

// 干跑调用第 index 项菜单（idCmdFirst=1），返回去引号后的命令行全文
static std::wstring invokeDry(IContextMenu* m, UINT index, const std::wstring& dryFile) {
    DeleteFileW(dryFile.c_str());
    CMINVOKECOMMANDINFO ci{};
    ci.cbSize = sizeof(ci);
    ci.lpVerb = MAKEINTRESOURCEA(1 + index);
    if (FAILED(m->InvokeCommand(&ci))) return L"<invoke failed>";
    for (int i = 0; i < 500 && GetFileAttributesW(dryFile.c_str()) == INVALID_FILE_ATTRIBUTES; ++i)
        Sleep(20);
    FILE* f = _wfopen(dryFile.c_str(), L"rb");
    if (f == nullptr) return L"<no dry-run output>";
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                         static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        w.data(), wlen);
    std::wstring flat;
    for (const wchar_t c : w)
        if (c != L'"' && c != L'\r') flat += c;
    return flat;
}

// Windows 路径大小写不敏感：背景模式的路径来自 SHGetPathFromIDListW 的
// 规范化大小写，可能与测试侧环境变量 TEMP 的大小写不同，必须忽略大小写比较
static std::wstring lowered(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

static bool hasAll(const std::wstring& line, const std::vector<std::wstring>& parts) {
    const std::wstring hl = lowered(line);
    for (const auto& p : parts)
        if (hl.find(lowered(p)) == std::wstring::npos) return false;
    return true;
}

static std::string toUtf8(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0,
                                      nullptr, nullptr);
    std::string s(n > 1 ? n - 1 : 0, '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

// 断言命令行包含各部分；失败时把实际内容打出来便于诊断
static void checkLine(bool ok, const std::wstring& line, const std::string& msg) {
    check(ok, ok ? msg : msg + " | 实际: " + toUtf8(line));
}

// UTF-16 码元十六进制转储：暴露不可见字符与真实内容
static std::string hexOf(const std::wstring& w) {
    std::string s;
    char buf[8];
    for (wchar_t c : w) {
        std::snprintf(buf, sizeof(buf), "%04x ", static_cast<unsigned>(c));
        s += buf;
    }
    return s;
}

static std::wstring quoteArg(const std::wstring& s) {
    return L"\"" + s + L"\"";
}

static bool fileExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// 端到端：真实 ggz.exe 走完整链路（造包 → 经 DLL 智能解压 → 文件落盘）
// 需要真实 ggz 路径参数；7z 引擎由外部环境变量 GGZIP_7Z 提供（子进程继承）
static void runE2E(IContextMenu* m, const std::wstring& ggz, const std::wstring& dir,
                   const std::wstring& dryFile) {
    SetEnvironmentVariableW(L"GGZIP_SHELL_DRYRUN", nullptr);   // 关闭干跑
    DeleteFileW(dryFile.c_str());

    const std::wstring f1 = dir + L"\\file1.txt";
    const std::wstring f2 = dir + L"\\file2.txt";
    writeDummy(f1);
    writeDummy(f2);
    const std::wstring zip = dir + L"\\real.zip";
    // 整条命令再包一层引号：_wsystem 经 cmd /c 执行，以引号开头且含特殊字符时
    // cmd 会剥掉首字符和最后一个引号（经典 cmd /c 引号坑），外层引号可保命令完整
    const int rc = _wsystem((L"\"" + quoteArg(ggz) + L" create " + quoteArg(zip) +
                             L" " + quoteArg(f1) + L" " + quoteArg(f2) +
                             L" >nul 2>&1" + L"\"").c_str());
    check(rc == 0 && fileExists(zip), "E2E: ggz create 生成 real.zip");
    if (rc != 0) return;

    IDataObject* obj = new TestDataObject(makeHDrop({ zip }));
    const MenuResult r = queryMenu(m, obj, nullptr);
    obj->Release();
    check(r.used == 3, "E2E: real.zip 弹出 3 个解压菜单项");

    CMINVOKECOMMANDINFO ci{};
    ci.cbSize = sizeof(ci);
    ci.lpVerb = MAKEINTRESOURCEA(1);   // 第 0 项 = 智能解压
    check(SUCCEEDED(m->InvokeCommand(&ci)), "E2E: InvokeCommand(智能解压)");

    // 多顶层项 → 智能解压应建 real\ 子文件夹
    bool ok = false;
    for (int i = 0; i < 300; ++i) {
        if (fileExists(dir + L"\\real\\file1.txt") &&
            fileExists(dir + L"\\real\\file2.txt")) { ok = true; break; }
        Sleep(100);
    }
    check(ok, "E2E: 智能解压产出 real\\file1.txt 与 real\\file2.txt");
}

int wmain(int argc, wchar_t** argv) {
    // 子模式：验证“注销后 CoCreateInstance 必须失败”
    // （父进程内 COM 可能缓存类工厂，必须用全新进程验证）
    if (argc >= 2 && wcscmp(argv[1], L"--verify-unregistered") == 0) {
        if (FAILED(CoInitialize(nullptr))) return 2;
        IContextMenu* m = nullptr;
        const HRESULT hr = CoCreateInstance(kClsid, nullptr, CLSCTX_INPROC_SERVER,
                                            IID_IContextMenu,
                                            reinterpret_cast<void**>(&m));
        if (m != nullptr) m->Release();
        CoUninitialize();
        std::printf(SUCCEEDED(hr) ? "STILL-REGISTERED\n" : "UNREGISTERED-OK\n");
        return SUCCEEDED(hr) ? 3 : 0;
    }
    if (argc < 2) {
        std::printf("usage: shell_host_test <ggzip_shell.dll> [ggz.exe]\n");
        return 2;
    }
    const std::wstring dll = argv[1];
    const std::wstring ggz = argc > 2 ? argv[2] : L"C:\\fake\\ggz.exe";

    if (FAILED(CoInitialize(nullptr))) {
        std::printf("CoInitialize 失败\n");
        return 2;
    }

    // 临时目录 + 测试文件
    wchar_t tmpRoot[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpRoot);
    std::error_code ec;
    const std::wstring dir = std::wstring(tmpRoot) + L"ggzip_shell_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::wstring aZip = dir + L"\\a.zip";
    const std::wstring docTxt = dir + L"\\doc.txt";
    const std::wstring photoJpg = dir + L"\\photo.jpg";
    writeDummy(aZip);
    writeDummy(docTxt);
    writeDummy(photoJpg);

    SetEnvironmentVariableW(L"GGZIP_GGZ", ggz.c_str());
    const std::wstring dryFile = dir + L"\\dry.txt";
    SetEnvironmentVariableW(L"GGZIP_SHELL_DRYRUN", dryFile.c_str());

    // 1. 注册（HKCU）
    check(runRegsvr32(L"/s \"" + dll + L"\"") == 0, "regsvr32 注册");
    IContextMenu* m = createMenu(true);

    // 2. 场景 A：单个压缩包 → 3 项
    {
        IDataObject* obj = new TestDataObject(makeHDrop({ aZip }));
        const MenuResult r = queryMenu(m, obj, nullptr);
        obj->Release();
        check(r.used == 3, "单个压缩包应有 3 个菜单项");
        check(labelsContain(r, L"智能解压"), "含智能解压");
        check(labelsContain(r, L"解压到当前文件夹"), "含解压到当前文件夹");
        check(labelsContain(r, L"解压到 \"a"), "含解压到同名文件夹");
        const std::wstring line = invokeDry(m, 0, dryFile);
        checkLine(hasAll(line, { L"smart-extract", aZip, L"-o " + dir }), line,
                  "智能解压命令行正确");
        const std::wstring line2 = invokeDry(m, 2, dryFile);
        checkLine(hasAll(line2, { L"extract", aZip, L"-o " + dir + L"\\a\n" }), line2,
                  "解压到子文件夹命令行正确");
        // GetCommandString（此时 m_items[0] = 智能解压）
        wchar_t wverb[64]{};
        const HRESULT hr1 = m->GetCommandString(0, GCS_VERBW, nullptr,
                                                reinterpret_cast<char*>(wverb), 64);
        check(SUCCEEDED(hr1) && wcscmp(wverb, L"ggzip.smart") == 0, "GCS_VERBW 返回动词");
        wchar_t whelp[256]{};
        const HRESULT hr2 = m->GetCommandString(0, GCS_HELPTEXTW, nullptr,
                                                reinterpret_cast<char*>(whelp), 256);
        check(SUCCEEDED(hr2) && whelp[0] != L'\0', "GCS_HELPTEXTW 返回帮助文本");
    }

    // 3. 场景 B：两个普通文件 → 2 项压缩
    {
        IDataObject* obj = new TestDataObject(makeHDrop({ docTxt, photoJpg }));
        const MenuResult r = queryMenu(m, obj, nullptr);
        obj->Release();
        check(r.used == 2, "普通文件应有 2 个菜单项");
        check(labelsContain(r, L"添加到 \"doc.7z\""), "含添加到 doc.7z");
        check(labelsContain(r, L"添加到 \"doc.zip\""), "含添加到 doc.zip");
        const std::wstring line = invokeDry(m, 0, dryFile);
        checkLine(hasAll(line, { L"create", dir + L"\\doc.7z", docTxt, photoJpg }),
                  line, "创建 7z 命令行正确");
    }

    // 4. 场景 C：压缩包 + 普通文件混选 → 4 项（解压组 2 项 + 压缩组 2 项；
    //    “解压到子文件夹”仅单压缩包时提供）
    {
        IDataObject* obj = new TestDataObject(makeHDrop({ aZip, docTxt }));
        const MenuResult r = queryMenu(m, obj, nullptr);
        obj->Release();
        check(r.used == 4, "混选应有 4 个菜单项");
        check(labelsContain(r, L"智能解压"), "混选含解压组");
        check(labelsContain(r, L"添加到 \"a.7z\""), "混选含压缩组（基名取第一项）");
    }

    // 5. 场景 D：文件夹空白处背景 → 2 项，基名为目录名
    {
        PIDLIST_ABSOLUTE pidl = nullptr;
        check(SUCCEEDED(SHParseDisplayName(dir.c_str(), nullptr, &pidl, 0, nullptr)) &&
                  pidl != nullptr, "SHParseDisplayName");
        const MenuResult r = queryMenu(m, nullptr, pidl);
        CoTaskMemFree(pidl);
        check(r.used == 2, "目录背景应有 2 个菜单项");
        check(labelsContain(r, L"添加到 \"ggzip_shell_test.7z\""), "背景模式基名为目录名");
        const std::wstring line = invokeDry(m, 0, dryFile);
        const std::wstring want = dir + L"\\ggzip_shell_test.7z";
        const bool ok = hasAll(line, { L"create", want });
        if (ok) check(true, "背景模式创建命令行正确");
        else {
            check(false, "背景模式创建命令行正确 | 期望含: " + toUtf8(want) +
                             " | dir_hex: " + hexOf(dir) +
                             " | 实际_hex: " + hexOf(line));
        }
    }

    // 5.5 可选 E2E：--e2e 且 ggz 路径真实存在
    bool e2e = false;
    for (int i = 1; i < argc; ++i)
        if (wcscmp(argv[i], L"--e2e") == 0) e2e = true;
    if (e2e && m != nullptr && ggz != L"C:\\fake\\ggz.exe")
        runE2E(m, ggz, dir, dryFile);

    if (m != nullptr) m->Release();

    // 6. 注销后，用全新子进程验证 CoCreateInstance 必须失败
    check(runRegsvr32(L"/u /s \"" + dll + L"\"") == 0, "regsvr32 注销");
    wchar_t self[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
    const int vrc = _wsystem((std::wstring(L"\"") + self +
                              L"\" --verify-unregistered").c_str());
    check(vrc == 0, "注销后新进程 CoCreateInstance 应失败");

    fs::remove_all(dir, ec);
    CoUninitialize();

    if (g_failed == 0) std::printf("\n全部通过\n");
    else std::printf("\n%d 项失败\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}

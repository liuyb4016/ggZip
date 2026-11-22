// ggz —— ggZip 命令行工具 (v0.1)
#include <windows.h>
#include "ggzip/ArchiveError.h"
#include "ggzip/EngineFactory.h"
#include "ggzip/Format.h"
#include "ggzip/IEngine.h"
#include "ggzip/SmartExtract.h"
#include "ggzip/Text.h"
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::atomic_bool g_cancel{ false };

BOOL WINAPI onCtrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        g_cancel.store(true);
        return TRUE;
    }
    return FALSE;
}

void out(const std::string& s) { fwrite(s.data(), 1, s.size(), stdout); }
void outln(const std::string& s) { out(s); out("\n"); }
void errln(const std::string& s) { fprintf(stderr, "%s\n", s.c_str()); }
void finishProgress() { fprintf(stderr, "\r           \r"); }

std::string humanSize(std::uint64_t n) {
    char buf[64];
    if (n < 1024) std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)n);
    else if (n < 1024ull * 1024)
        std::snprintf(buf, sizeof(buf), "%.1f KB", (double)n / 1024.0);
    else if (n < 1024ull * 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%.1f MB", (double)n / 1024.0 / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%.2f GB", (double)n / 1024.0 / 1024.0 / 1024.0);
    return buf;
}

std::string col(const std::string& s, size_t w) {
    if (s.size() >= w) return s;
    return s + std::string(w - s.size(), ' ');
}

void printProgress(const ggzip::Progress& p) {
    static std::mutex m;
    static double last = -1;
    std::lock_guard<std::mutex> g(m);
    if (p.percent >= 0 && p.percent != last) {
        last = p.percent;
        fprintf(stderr, "\r%5.1f%% ", p.percent);
    }
}

const char* kUsage =
    "ggz —— ggZip 命令行工具\n"
    "\n"
    "用法:\n"
    "  ggz list <压缩包>                                  列出内容\n"
    "  ggz extract <压缩包> [-o 目录] [-p 密码]            解压\n"
    "  ggz smart-extract <压缩包> [-o 目录] [-p 密码]      智能解压\n"
    "  ggz create <压缩包> <输入...> [-p 密码] [--level N] [--threads N]  压缩\n"
    "  ggz test <压缩包> [-p 密码]                        完整性测试\n"
    "  ggz where                                          显示压缩引擎路径\n"
    "\n"
    "示例:\n"
    "  ggz create backup.7z D:\\docs --level 7\n"
    "  ggz smart-extract tool.tar.gz -o D:\\tools\n"
    "\n"
    "说明: 压缩引擎为 7z.exe（自动定位：环境变量 GGZIP_7Z -> 程序同目录 -> PATH -> Program Files）。\n"
    "创建支持: 7z / zip / tar / gz / bz2 / xz / zst / tar.gz / tar.bz2 / tar.xz；\n"
    "解压支持 7z 引擎的全部 40+ 格式（zip/7z/rar/iso/cab/wim/msi 等）。\n";

struct Args {
    std::wstring archive;
    std::wstring outDir;
    std::wstring password;
    std::vector<std::wstring> inputs;
    int level = 5;
    int threads = 0;
    bool smart = false;
};

bool parseArgs(int argc, wchar_t** argv, std::wstring& cmd, Args& a) {
    if (argc < 2) return false;
    cmd = argv[1];
    if (cmd == L"--help" || cmd == L"-h" || cmd == L"help") return false;

    std::vector<std::wstring> positional;
    for (int i = 2; i < argc; ++i) {
        std::wstring s = argv[i];
        if (s == L"--smart") { a.smart = true; }
        else if (s == L"--level" && i + 1 < argc) { a.level = _wtoi(argv[++i]); }
        else if (s == L"--threads" && i + 1 < argc) { a.threads = _wtoi(argv[++i]); }
        else if (s == L"-o" && i + 1 < argc) { a.outDir = argv[++i]; }
        else if (s.rfind(L"-o", 0) == 0 && s.size() > 2) { a.outDir = s.substr(2); }
        else if (s == L"-p" && i + 1 < argc) { a.password = argv[++i]; }
        else if (s.rfind(L"-p", 0) == 0 && s.size() > 2) { a.password = s.substr(2); }
        else if (s == L"-h" || s == L"--help") { return false; }
        else positional.push_back(s);
    }
    if (!positional.empty()) a.archive = positional[0];
    for (size_t k = 1; k < positional.size(); ++k) a.inputs.push_back(positional[k]);
    return true;
}

// ---------- 子命令 ----------

int cmdList(ggzip::IEngine& e, const Args& a) {
    std::vector<ggzip::Entry> entries;
    auto info = e.inspect(a.archive, &entries);
    outln("压缩包: " + ggzip::utf8(a.archive));
    outln(std::string("格式:   ") + (info.typeName.empty() ? "unknown" : info.typeName) +
          "    物理大小: " + humanSize(info.physicalSize) +
          "    条目: " + std::to_string(entries.size()));
    outln("");
    for (const auto& en : entries) {
        out("  " + col(en.isDir ? "<DIR>" : humanSize(en.size), 12));
        out(col(en.isDir ? "" : humanSize(en.packedSize), 12));
        outln("  " + ggzip::utf8(en.path));
    }
    return 0;
}

// 解压（智能解压 / tar.* 双层解包逻辑在 core 的 extractSmart 中）
int cmdExtract(ggzip::IEngine& e, const Args& a) {
    fs::path target = a.outDir.empty() ? fs::current_path() : fs::path(a.outDir);
    ggzip::ExtractOptions opt;
    opt.password = a.password;

    auto realOut = ggzip::extractSmart(e, a.archive, target.wstring(), a.smart,
                                       opt, printProgress, g_cancel);
    finishProgress();
    if (g_cancel.load()) { errln("已取消"); return 3; }
    outln("已解压到: " + ggzip::utf8(realOut));
    return 0;
}

int cmdCreate(ggzip::IEngine& e, const Args& a) {
    ggzip::CreateOptions opt;
    opt.password = a.password;
    opt.level = a.level;
    opt.threads = a.threads;
    e.create(a.archive, a.inputs, opt, printProgress, g_cancel);
    finishProgress();
    if (g_cancel.load()) { errln("已取消"); return 3; }
    outln("已创建: " + ggzip::utf8(a.archive));
    return 0;
}

int cmdTest(ggzip::IEngine& e, const Args& a) {
    e.test(a.archive, a.password, printProgress, g_cancel);
    finishProgress();
    if (g_cancel.load()) { errln("已取消"); return 3; }
    outln("OK: " + ggzip::utf8(a.archive));
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(onCtrl, TRUE);
    try {
        std::wstring cmd;
        Args a;
        if (!parseArgs(argc, argv, cmd, a)) { outln(kUsage); return 1; }

        auto engine = ggzip::createEngine();
        if (!engine->available()) {
            errln("未找到 7z.exe 压缩引擎。\n"
                  "请安装 7-Zip（https://www.7-zip.org），或将 7z.exe 放在 ggz.exe 同目录，\n"
                  "或设置环境变量 GGZIP_7Z 指向 7z.exe。");
            return 1;
        }

        if (cmd == L"list") {
            if (a.archive.empty()) { outln(kUsage); return 1; }
            return cmdList(*engine, a);
        }
        if (cmd == L"extract") {
            if (a.archive.empty()) { outln(kUsage); return 1; }
            return cmdExtract(*engine, a);
        }
        if (cmd == L"smart-extract") {
            if (a.archive.empty()) { outln(kUsage); return 1; }
            a.smart = true;
            return cmdExtract(*engine, a);
        }
        if (cmd == L"create") {
            if (a.archive.empty() || a.inputs.empty()) { outln(kUsage); return 1; }
            return cmdCreate(*engine, a);
        }
        if (cmd == L"test") {
            if (a.archive.empty()) { outln(kUsage); return 1; }
            return cmdTest(*engine, a);
        }
        if (cmd == L"where") {
            outln(ggzip::utf8(engine->enginePath()));
            return 0;
        }
        if (cmd == L"version" || cmd == L"--version") {
            outln("ggz (ggZip) 0.2.1");
            return 0;
        }
        outln(kUsage);
        return 1;
    } catch (const ggzip::ArchiveError& e) {
        finishProgress();
        errln(std::string("错误: ") + e.what());
        return 2;
    } catch (const std::exception& e) {
        finishProgress();
        errln(std::string("错误: ") + e.what());
        return 2;
    }
}

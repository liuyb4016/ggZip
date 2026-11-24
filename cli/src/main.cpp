// ggz —— ggZip 命令行工具 (v0.1)
#include <windows.h>
#include "ggzip/ArchiveError.h"
#include "ggzip/Diagnostics.h"
#include "ggzip/ArchiveVolumes.h"
#include "ggzip/OperationError.h"
#include "ggzip/EngineFactory.h"
#include "ggzip/Format.h"
#include "ggzip/IEngine.h"
#include "ggzip/PasswordStore.h"
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
#include <set>
#include <algorithm>

namespace fs = std::filesystem;
using ggzip::diagnostic;

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

void printArchiveError(const ggzip::ArchiveError& error) {
    errln(std::string("[") + error.code() + "] " + error.what());
}

std::vector<std::wstring> languageArguments(int argc, wchar_t** argv) {
    std::vector<std::wstring> arguments{argv[0]};
    bool literal = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring value = argv[index];
        if (!literal && (value == L"--lang" || value.rfind(L"--lang=", 0) == 0)) {
            std::wstring selected;
            if (value == L"--lang") {
                if (index + 1 == argc)
                    throw ggzip::ArchiveError(diagnostic("语言选项缺少值，请使用 --lang zh_CN 或 --lang en"));
                selected = argv[++index];
            } else selected = value.substr(7);
            if (selected == L"en") ggzip::setDiagnosticLanguage(ggzip::DiagnosticLanguage::English);
            else if (selected == L"zh_CN") ggzip::setDiagnosticLanguage(ggzip::DiagnosticLanguage::SimplifiedChinese);
            else throw ggzip::ArchiveError(diagnostic("无效的语言选项，请使用 zh_CN 或 en"));
            continue;
        }
        arguments.push_back(value);
        if (literal) continue;
        if (value == L"--") literal = true;
        else if ((value == L"-p" || value == L"-o" || value == L"--on-conflict" ||
            value == L"--volume" || value == L"--level" || value == L"--threads") && index + 1 < argc)
            arguments.push_back(argv[++index]);
    }
    return arguments;
}

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
    "  ggz list <压缩包> [-p 密码]                        列出内容\n"
    "  ggz extract <压缩包> [-o 目录] [-p 密码]            解压\n"
    "  ggz smart-extract <压缩包> [-o 目录] [-p 密码]      智能解压\n"
    "  ggz batch-extract <压缩包...> [-o 目录] [--continue-on-error]  串行批量解压\n"
    "  ggz create <压缩包> <输入...> [-p 密码] [--level N] [--threads N]\n"
    "         [--volume 大小]                              压缩（--volume 100m 为分卷）\n"
    "  ggz test <压缩包> [-p 密码]                        完整性测试\n"
    "  ggz delete <压缩包> <条目...> [-p 密码]            删除 ZIP/7z 内条目\n"
    "  ggz rename <压缩包> <旧路径> <新路径> [-p 密码]    重命名 ZIP/7z 内条目\n"
    "  ggz add <压缩包> <文件...> [-p 密码]               添加 ZIP/7z 内条目\n"
    "  ggz pass list | add <标签> -p <密码> | remove <标签>   密码库管理（DPAPI 加密）\n"
    "  ggz where                                          显示压缩引擎路径\n"
    "\n"
    "示例:\n"
    "  ggz create backup.7z D:\\docs --level 7\n"
    "  ggz smart-extract tool.tar.gz -o D:\\tools\n"
    "\n"
    "语言: --lang zh_CN|en，可放在命令前后；-- 后的参数按字面量处理。\n"
    "说明: 使用随包内置的 7z.exe，无需另装 7-Zip；源码开发可设置 GGZIP_7Z。\n"
    "冲突策略: --on-conflict error|overwrite|skip|rename（默认 error，绝不隐式覆盖）。\n"
    "创建支持: 7z / zip / tar / gz / bz2 / xz / zst / tar.gz / tar.bz2 / tar.xz；\n"
    "解压支持 7z 引擎的全部 40+ 格式（zip/7z/rar/iso/cab/wim/msi 等）。\n";

const char* kUsageEnglish =
    "ggz - ggZip command-line tool\n"
    "\n"
    "Usage:\n"
    "  ggz list <archive> [-p password]                 List contents\n"
    "  ggz extract <archive> [-o directory] [-p password]\n"
    "  ggz smart-extract <archive> [-o directory] [-p password]\n"
    "  ggz batch-extract <archives...> [-o directory] [--continue-on-error]\n"
    "  ggz create <archive> <inputs...> [-p password] [--level N] [--threads N]\n"
    "         [--volume size]                           Create (e.g. --volume 100m)\n"
    "  ggz test <archive> [-p password]                 Integrity test\n"
    "  ggz delete <archive> <members...> [-p password]  Delete ZIP/7z entries\n"
    "  ggz rename <archive> <old> <new> [-p password]    Rename ZIP/7z entries\n"
    "  ggz add <archive> <files...> [-p password]       Add ZIP/7z entries\n"
    "  ggz pass list | add <label> -p <password> | remove <label>\n"
    "         Manage the DPAPI-encrypted password store\n"
    "  ggz where                                      Show engine path\n"
    "  ggz version                                    Show version\n"
    "\n"
    "Examples:\n"
    "  ggz --lang en create backup.7z D:\\docs --level 7\n"
    "  ggz smart-extract tool.tar.gz -o D:\\tools --lang en\n"
    "\n"
    "Language: --lang zh_CN|en before or after the command; arguments after -- are literal.\n"
    "Uses bundled 7z.exe; no separate 7-Zip installation is required.\n"
    "Source developers may set GGZIP_7Z.\n"
    "Conflicts: --on-conflict error|overwrite|skip|rename (default: error; never implicit overwrite).\n"
    "Creation: 7z / zip / tar / gz / bz2 / xz / zst / tar.gz / tar.bz2 / tar.xz.\n"
    "Extraction: all 40+ formats supported by the 7z engine, including RAR.\n";

const char* usageText() {
    return ggzip::diagnosticLanguage() == ggzip::DiagnosticLanguage::English ? kUsageEnglish : kUsage;
}

struct Args {
    std::wstring archive;
    std::wstring outDir;
    std::wstring password;
    std::wstring volumeSize;   // 分卷大小（create），如 100m；空 = 不分卷
    std::vector<std::wstring> inputs;
    int level = 5;
    int threads = 0;
    bool smart = false;
    bool continueOnError = false;
    bool help = false;
    ggzip::ConflictPolicy conflict = ggzip::ConflictPolicy::Reject;
};

bool parseArgs(int argc, wchar_t** argv, std::wstring& cmd, Args& a) {
    if (argc < 2) return false;
    cmd = argv[1];
    if (cmd == L"--help" || cmd == L"-h" || cmd == L"help") { a.help = true; return false; }

    std::vector<std::wstring> positional;
    bool literal = false;
    for (int i = 2; i < argc; ++i) {
        std::wstring s = argv[i];
        if (literal) { positional.push_back(s); continue; }
        if (s == L"--") { literal = true; continue; }
        if (s == L"--smart") { a.smart = true; }
        else if (s == L"--continue-on-error") { a.continueOnError = true; }
        else if (s == L"--on-conflict" && i + 1 < argc) {
            const std::wstring value = argv[++i];
            if (value == L"error") a.conflict = ggzip::ConflictPolicy::Reject;
            else if (value == L"overwrite") a.conflict = ggzip::ConflictPolicy::Overwrite;
            else if (value == L"skip") a.conflict = ggzip::ConflictPolicy::Skip;
            else if (value == L"rename") a.conflict = ggzip::ConflictPolicy::Rename;
            else throw ggzip::ConflictError(diagnostic("无效的覆盖策略"));
        }
        else if (s == L"--volume" && i + 1 < argc) { a.volumeSize = argv[++i]; }
        else if (s == L"--level" && i + 1 < argc) { a.level = _wtoi(argv[++i]); }
        else if (s == L"--threads" && i + 1 < argc) { a.threads = _wtoi(argv[++i]); }
        else if (s == L"-o" && i + 1 < argc) { a.outDir = argv[++i]; }
        else if (s.rfind(L"-o", 0) == 0 && s.size() > 2) { a.outDir = s.substr(2); }
        else if (s == L"-p" && i + 1 < argc) { a.password = argv[++i]; }
        else if (s.rfind(L"-p", 0) == 0 && s.size() > 2) { a.password = s.substr(2); }
        else if (s == L"-h" || s == L"--help") { a.help = true; return false; }
        else positional.push_back(s);
    }
    if (!positional.empty()) a.archive = positional[0];
    for (size_t k = 1; k < positional.size(); ++k) a.inputs.push_back(positional[k]);
    return true;
}

// ---------- 子命令 ----------

int cmdList(ggzip::IEngine& e, const Args& a) {
    std::vector<ggzip::Entry> entries;
    auto info = e.inspect(a.archive, &entries, {a.password, true});
    outln(diagnostic("压缩包: ") + ggzip::utf8(a.archive));
    outln(std::string(diagnostic("格式:   ")) + (info.typeName.empty() ? "unknown" : info.typeName) +
          diagnostic("    物理大小: ") + humanSize(info.physicalSize) +
          diagnostic("    条目: ") + std::to_string(entries.size()));
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
    opt.conflict = a.conflict;

    auto result = ggzip::extractSmart(e, a.archive, target.wstring(), a.smart,
                                       opt, printProgress, g_cancel);
    finishProgress();
    outln(diagnostic("完成 ") + std::to_string(result.completed) + diagnostic(" 项，跳过 ") + std::to_string(result.skipped) +
          diagnostic(" 项，输出: ") + ggzip::utf8(result.destination));
    if (result.outcome == ggzip::Outcome::Cancelled) { errln(diagnostic("已取消，完整新文件已保留")); return 3; }
    return 0;
}

int cmdCreate(ggzip::IEngine& e, const Args& a) {
    ggzip::CreateOptions opt;
    opt.password = a.password;
    opt.level = a.level;
    opt.threads = a.threads;
    opt.volumeSize = a.volumeSize;
    opt.conflict = a.conflict;
    const auto result = e.create(a.archive, a.inputs, opt, printProgress, g_cancel);
    finishProgress();
    if (result.outcome == ggzip::Outcome::Cancelled) { errln(diagnostic("已取消，原压缩包未改变")); return 3; }
    outln(result.skipped ? diagnostic("已跳过现有压缩包") : diagnostic("已创建: ") + ggzip::utf8(result.destination));
    return 0;
}

int cmdBatchExtract(ggzip::IEngine& engine, const Args& arguments) {
    std::vector<std::wstring> archives{arguments.archive};
    archives.insert(archives.end(), arguments.inputs.begin(), arguments.inputs.end());
    int failed = 0;
    std::set<std::wstring> seen;
    for (const auto& archive : archives) {
        if (g_cancel.load()) return 3;
        ggzip::ExtractOptions options;
        options.password = arguments.password;
        options.conflict = arguments.conflict;
        try {
            const auto volumes = ggzip::archiveVolumes(archive);
            const fs::path source = volumes.first;
            auto key = source.wstring();
            std::transform(key.begin(), key.end(), key.begin(),
                [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
            if (!seen.insert(key).second) continue;
            const fs::path parent = arguments.outDir.empty() ? source.parent_path() : fs::absolute(arguments.outDir);
            const auto& name = volumes.baseName;
            const auto directory = ggzip::dedupeFolderName(parent.wstring(), name.empty() ? L"extracted" : name);
            const auto result = engine.extract(source.wstring(), (parent / directory).wstring(), {}, options, printProgress, g_cancel);
            finishProgress();
            outln(diagnostic("完成: ") + ggzip::utf8(source.wstring()) + " -> " + ggzip::utf8(result.destination));
            if (result.outcome == ggzip::Outcome::Cancelled) return 3;
        } catch (const std::exception& error) {
            finishProgress();
            if (g_cancel.load()) return 3;
            ++failed;
            errln(diagnostic("失败: ") + ggzip::utf8(archive) + "\n" + error.what());
            if (!arguments.continueOnError) return 1;
        }
    }
    return failed ? 1 : 0;
}

int cmdTest(ggzip::IEngine& e, const Args& a) {
    e.test(a.archive, a.password, printProgress, g_cancel);
    finishProgress();
    if (g_cancel.load()) { errln(diagnostic("已取消")); return 3; }
    outln("OK: " + ggzip::utf8(a.archive));
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(onCtrl, TRUE);
    try {
        auto normalized = languageArguments(argc, argv);
        std::vector<wchar_t*> pointers;
        for (auto& argument : normalized) pointers.push_back(argument.data());
        std::wstring cmd;
        Args a;
        if (!parseArgs(static_cast<int>(pointers.size()), pointers.data(), cmd, a)) { outln(usageText()); return a.help ? 0 : 1; }

        auto engine = ggzip::createEngine();
        const bool needsEngine = (cmd == L"list" || cmd == L"extract" ||
                                  cmd == L"smart-extract" || cmd == L"create" ||
                                  cmd == L"test" || cmd == L"delete" || cmd == L"rename" || cmd == L"add" || cmd == L"batch-extract");
        if (needsEngine && !engine->available()) {
            printArchiveError(ggzip::ArchiveError(std::string(diagnostic("内置 7-Zip 引擎文件缺失。请重新安装或完整解压 ggZip，保留 7z 子目录。")) +
                  diagnostic("源码开发时可设置 GGZIP_7Z 指向自备的 7z.exe。")));
            return 1;
        }

        if (cmd == L"list") {
            if (a.archive.empty()) { outln(usageText()); return 1; }
            return cmdList(*engine, a);
        }
        if (cmd == L"extract") {
            if (a.archive.empty()) { outln(usageText()); return 1; }
            return cmdExtract(*engine, a);
        }
        if (cmd == L"smart-extract") {
            if (a.archive.empty()) { outln(usageText()); return 1; }
            a.smart = true;
            return cmdExtract(*engine, a);
        }
        if (cmd == L"batch-extract") {
            if (a.archive.empty()) { outln(usageText()); return 1; }
            return cmdBatchExtract(*engine, a);
        }
        if (cmd == L"create") {
            if (a.archive.empty() || a.inputs.empty()) { outln(usageText()); return 1; }
            return cmdCreate(*engine, a);
        }
        if (cmd == L"pass") {
            const std::wstring sub = a.archive;   // 第一个位置参数 = 子命令
            const std::wstring label =
                a.inputs.empty() ? std::wstring() : a.inputs[0];
            ggzip::PasswordStore store;
            if (sub == L"list") {
                auto es = store.load();
                if (es.empty()) { outln(diagnostic("密码库为空")); return 0; }
                for (const auto& e : es)
                    outln("  " + ggzip::utf8(e.label) + diagnostic("  （密码 ") +
                          std::to_string(e.password.size()) + diagnostic(" 字符）"));
                return 0;
            }
            if (sub == L"add") {
                if (label.empty() || a.password.empty()) {
                    outln(diagnostic("用法: ggz pass add <标签> -p <密码>"));
                    return 1;
                }
                store.add(label, a.password);
                outln(diagnostic("已保存密码: ") + ggzip::utf8(label));
                return 0;
            }
            if (sub == L"remove") {
                if (label.empty()) { outln(diagnostic("用法: ggz pass remove <标签>")); return 1; }
                outln(store.remove(label) ? diagnostic("已删除: ") + ggzip::utf8(label)
                                          : diagnostic("未找到: ") + ggzip::utf8(label));
                return 0;
            }
            outln(diagnostic("用法: ggz pass list | add <标签> -p <密码> | remove <标签>"));
            return 1;
        }
        if (cmd == L"test") {
            if (a.archive.empty()) { outln(usageText()); return 1; }
            return cmdTest(*engine, a);
        }
        if (cmd == L"delete" || cmd == L"rename" || cmd == L"add") {
            if (a.archive.empty() || a.inputs.empty()) { outln(usageText()); return 1; }
            ggzip::EditOptions options;
            options.password = a.password;
            options.conflict = a.conflict;
            if (cmd == L"delete") options.remove = a.inputs;
            else if (cmd == L"rename") {
                if (a.inputs.size() != 2) { outln(usageText()); return 1; }
                options.rename.push_back({a.inputs[0], a.inputs[1]});
            } else {
                for (const auto& input : a.inputs) options.add.push_back({input, fs::path(input).filename().wstring()});
            }
            const auto result = engine->edit(a.archive, options, printProgress, g_cancel);
            finishProgress();
            if (result.outcome == ggzip::Outcome::Cancelled) { errln(diagnostic("已取消，原压缩包未改变")); return 3; }
            outln(diagnostic("已编辑: ") + ggzip::utf8(result.destination));
            return 0;
        }
        if (cmd == L"where") {
            outln(ggzip::utf8(engine->enginePath()));
            return 0;
        }
        if (cmd == L"version" || cmd == L"--version") {
            outln("ggz (ggZip) " GGZIP_VERSION);
            return 0;
        }
        outln(usageText());
        return 1;
    } catch (const ggzip::ConflictError& error) {
        finishProgress();
        printArchiveError(error);
        return 4;
    } catch (const ggzip::OperationError& error) {
        finishProgress();
        printArchiveError(error);
        errln(diagnostic("已保留完整文件: ") + std::to_string(error.summary.completed));
        return 1;
    } catch (const ggzip::ArchiveError& e) {
        finishProgress();
        if (g_cancel.load()) { errln(diagnostic("已取消")); return 3; }
        printArchiveError(e);
        return 2;
    } catch (const std::exception& e) {
        finishProgress();
        if (g_cancel.load()) { errln(diagnostic("已取消")); return 3; }
        errln(std::string(diagnostic("错误: ")) + e.what());
        return 2;
    }
}

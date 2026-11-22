// ggZip 冒烟测试：验证核心引擎的创建/列表/解压/加密/智能解压。
// 运行前需能找到 7z.exe 或 7za.exe（环境变量 GGZIP_7Z 或标准安装位置）。
#include <windows.h>
#include "ggzip/ArchiveError.h"
#include "ggzip/EngineFactory.h"
#include "ggzip/Format.h"
#include "ggzip/IEngine.h"
#include "ggzip/SmartExtract.h"
#include "ggzip/Text.h"
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::atomic_bool g_cancel{ false };
int g_failures = 0;
const ggzip::ProgressFn kNoProgress{};

void check(bool ok, const std::string& msg) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", msg.c_str());
    if (!ok) ++g_failures;
}

std::string readBin(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

void writeBin(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(data.data(), (std::streamsize)data.size());
}

bool hasEntry(const std::vector<ggzip::Entry>& es, const std::wstring& path,
              bool mustBeFile) {
    for (const auto& e : es) {
        if (e.path == path) return !mustBeFile || !e.isDir;
        // 目录路径以 '/' 结尾存储，允许不带尾斜杠比较
        if (!e.path.empty() && e.path.back() == L'/' &&
            e.path.substr(0, e.path.size() - 1) == path)
            return !mustBeFile;
    }
    return false;
}

// 若 extract 后目录里只有一个 .tar（tar.* 两层格式），再解一层
void unwrapTar(ggzip::IEngine& e, const fs::path& outDir,
               const ggzip::ExtractOptions& opt) {
    std::error_code ec;
    size_t count = 0;
    fs::path only;
    for (const auto& de : fs::directory_iterator(outDir, ec)) {
        ++count;
        if (count == 1) only = de.path();
    }
    if (count == 1) {
        std::wstring ext = only.extension().wstring();
        for (auto& c : ext) c = (wchar_t)std::towlower(c);
        if (ext == L".tar") {
            e.extract(only.wstring(), outDir.wstring(), {}, opt, kNoProgress, g_cancel);
            fs::remove(only, ec);
        }
    }
}

} // namespace

int wmain() {
    auto engine = ggzip::createEngine();
    if (!engine->available()) {
        std::printf("FAIL: 未找到 7z.exe（设置 GGZIP_7Z 或安装 7-Zip）\n");
        return 1;
    }
    std::printf("engine: %s\n", ggzip::utf8(engine->enginePath()).c_str());

    try {
        fs::path root = fs::temp_directory_path() /
                        ("ggzip_smoke_" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(root);
        fs::path src = root / "src";
        fs::create_directories(src);

        // 准备测试数据：普通文件 / 中文路径 / 随机二进制
        writeBin(src / "hello.txt", "hello ggzip\n");
        writeBin(src / L"docs" / L"指南" / L"中文文档.txt", "中文内容测试\n");
        writeBin(src / "docs" / "readme.md", "# ggzip smoke\n");
        std::mt19937 rng(1234);
        std::string big(3 * 1024 * 1024, '\0');
        for (auto& c : big) c = (char)(rng() & 0xFF);
        writeBin(src / "big.bin", big);

        ggzip::CreateOptions fast;
        fast.level = 1;

        // 1) 各格式创建 -> 列表 -> 解压 -> 内容比对
        struct Fmt { const wchar_t* ext; bool tarWrapped; };
        const std::vector<Fmt> fmts{ { L".zip", false }, { L".7z", false },
                                     { L".tar", false }, { L".tar.gz", true } };
        for (const auto& fm : fmts) {
            std::string tag = ggzip::utf8(fm.ext);
            std::wstring arc = (root / (std::wstring(L"test") + fm.ext)).wstring();
            engine->create(arc, { src.wstring() + L"\\*" }, fast, kNoProgress, g_cancel);
            check(fs::exists(arc), "create " + tag);

            auto entries = engine->list(arc);
            bool listOk;
            std::string listTag;
            if (fm.tarWrapped) {
                // -slt 对 tar.* 只显示内层 .tar 单条目（引擎行为）
                listOk = entries.size() == 1 && !entries.empty() &&
                         entries[0].path.size() >= 4 &&
                         entries[0].path.substr(entries[0].path.size() - 4) == L".tar";
                listTag = "list 内层 tar 条目 " + tag;
            } else {
                bool hasHello = hasEntry(entries, L"hello.txt", true);
                bool hasZh = false;
                for (const auto& en : entries)
                    if (en.path.find(L"中文文档.txt") != std::wstring::npos) hasZh = true;
                listOk = hasHello && hasZh;
                listTag = "list 含中文名条目 " + tag;
            }
            check(listOk, listTag);

            fs::path outd = root / ("out" + tag);
            fs::remove_all(outd);
            fs::create_directories(outd);
            engine->extract(arc, outd.wstring(), {}, {}, kNoProgress, g_cancel);
            if (fm.tarWrapped) unwrapTar(*engine, outd, {});

            check(readBin(outd / "hello.txt") == "hello ggzip\n", "内容回读 hello.txt " + tag);
            check(readBin(outd / L"docs" / L"指南" / L"中文文档.txt") == "中文内容测试\n",
                  "内容回读 中文文档 " + tag);
            check(readBin(outd / "big.bin") == big, "内容回读 big.bin " + tag);
        }

        // 2) 7z 加密：无密码失败、有密码成功
        {
            ggzip::CreateOptions po;
            po.password = L"密码123";
            std::wstring enc = (root / L"enc.7z").wstring();
            engine->create(enc, { (src / L"hello.txt").wstring() }, po, kNoProgress, g_cancel);

            bool threw = false;
            try {
                engine->extract(enc, (root / L"outenc0").wstring(), {}, {},
                                kNoProgress, g_cancel);
            } catch (const ggzip::ArchiveError&) { threw = true; }
            check(threw, "加密包无密码解压应失败");

            ggzip::ExtractOptions eo;
            eo.password = L"密码123";
            engine->extract(enc, (root / L"outenc").wstring(), {}, eo, kNoProgress, g_cancel);
            check(readBin(root / L"outenc" / L"hello.txt") == "hello ggzip\n",
                  "加密包带密码解压成功");
        }

        // 3) 完整性测试命令
        {
            bool ok = true;
            try {
                engine->test((root / L"test.zip").wstring(), L"", kNoProgress, g_cancel);
            } catch (const ggzip::ArchiveError&) { ok = false; }
            check(ok, "test(zip) 完整性测试");
        }

        // 4) 智能解压决策
        {
            fs::path srcA = root / "srcA";
            writeBin(srcA / "data" / "f1.txt", "A\n");
            std::wstring single = (root / L"single.7z").wstring();
            engine->create(single, { (srcA / L"data").wstring() }, fast, kNoProgress, g_cancel);
            auto planA = ggzip::smartPlan(engine->list(single));
            check(!planA.intoSubfolder, "智能解压: 单根目录 -> 直接解压");

            fs::path srcB = root / "srcB";
            writeBin(srcB / "a.txt", "B\n");
            writeBin(srcB / "sub" / "b.txt", "B\n");
            std::wstring messy = (root / L"messy.7z").wstring();
            // 用通配符打包目录内容（裸目录会把目录本身作为根）
            engine->create(messy, { srcB.wstring() + L"\\*" }, fast, kNoProgress, g_cancel);
            auto planB = ggzip::smartPlan(engine->list(messy));
            check(planB.intoSubfolder, "智能解压: 顶层多项 -> 应建子文件夹");
        }

        fs::remove_all(root);
        if (g_failures == 0) {
            std::printf("\n全部通过\n");
            return 0;
        }
        std::printf("\n失败 %d 项\n", g_failures);
        return 1;
    } catch (const ggzip::ArchiveError& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
}

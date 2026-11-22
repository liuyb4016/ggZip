#include "SevenZipEngine.h"
#include "ProcessUtil.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/Format.h"
#include "ggzip/Text.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

namespace ggzip {
namespace {

bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void requireFile(const std::wstring& p, const char* what) {
    if (!fileExists(p)) throw ArchiveError(std::string(what) + ": 文件不存在 - " + utf8(p));
}

// 从 -bsp1 进度流缓冲里解析最近的 "NN%"；解析不到返回 -1。
int parsePercent(const std::string& buf) {
    for (size_t i = buf.size(); i > 0; --i) {
        if (buf[i - 1] == '%') {
            size_t minB = (i >= 4) ? i - 4 : 0;
            size_t b = i - 1;
            while (b > minB && std::isdigit((unsigned char)buf[b - 1])) --b;
            if (b < i - 1) {
                int v = std::atoi(buf.c_str() + b);
                if (v >= 0 && v <= 100) return v;
            }
            return -1;
        }
    }
    return -1;
}

struct Listing {
    ArchiveInfo info;
    std::vector<Entry> entries;
};

// 解析 `7z l -slt -sccUTF-8` 输出。
// 每个块都从 "Path = " 行开始；含 "Size" 字段的是条目块，
// 否则是压缩包自身的元数据块（Type / Physical Size）。
Listing parseListing(const std::string& text) {
    Listing out;
    std::map<std::string, std::string> cur;

    auto flush = [&]() {
        if (cur.empty()) return;
        std::map<std::string, std::string> fields = std::move(cur);
        cur.clear();

        auto num = [&fields](const char* key) -> std::uint64_t {
            auto it = fields.find(key);
            return it == fields.end() ? 0 : std::strtoull(it->second.c_str(), nullptr, 10);
        };
        auto str = [&fields](const char* key) -> std::string {
            auto it = fields.find(key);
            return it == fields.end() ? std::string() : it->second;
        };

        if (!fields.count("Size")) {
            // 元数据块
            out.info.typeName = str("Type");
            out.info.physicalSize = num("Physical Size");
            return;
        }

        auto itPath = fields.find("Path");
        if (itPath == fields.end() || itPath->second.empty()) return;

        Entry e;
        std::wstring raw = wideFromUtf8(itPath->second);
        std::replace(raw.begin(), raw.end(), L'\\', L'/');
        e.path = raw;
        e.size = num("Size");
        e.packedSize = num("Packed Size");

        e.isDir = (str("Folder") == "+");
        if (!e.isDir) {
            auto itAttr = fields.find("Attributes");
            if (itAttr != fields.end() && !itAttr->second.empty())
                e.isDir = (itAttr->second[0] == 'D');
        }
        if (!e.path.empty() && e.path.back() == L'/') e.isDir = true;
        if (e.isDir && !e.path.empty() && e.path.back() != L'/') e.path += L'/';

        e.encrypted = (str("Encrypted") == "+");
        e.crc32 = (std::uint32_t)std::strtoul(str("CRC").c_str(), nullptr, 16);
        e.modified = str("Modified");
        e.method = str("Method");

        out.entries.push_back(std::move(e));
    };

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        std::string_view line(text);
        if (eol == std::string::npos) {
            line = line.substr(pos);
            pos = text.size() + 1;
        } else {
            line = line.substr(pos, eol - pos);
            pos = eol + 1;
        }
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;

        if (line.rfind("Path = ", 0) == 0) {
            flush();
            cur["Path"] = std::string(line.substr(7));
            continue;
        }
        auto eq = line.find(" = ");
        if (eq != std::string_view::npos)
            cur[std::string(line.substr(0, eq))] = std::string(line.substr(eq + 3));
    }
    flush();
    out.info.format = formatFromTypeName(out.info.typeName);
    return out;
}

} // namespace

bool SevenZipEngine::available() const { return !exe_.empty(); }

std::wstring SevenZipEngine::enginePath() const { return exe_; }

std::wstring SevenZipEngine::resolveFirstVolume(const std::wstring& path) const {
    if (fileExists(path)) return path;
    const std::wstring v1 = path + L".001";
    if (fileExists(v1)) return v1;
    return path;  // 保持原路径，requireFile 会给出清晰错误
}

void SevenZipEngine::ensure() const {
    if (exe_.empty())
        throw ArchiveError("未找到 7z.exe 压缩引擎。请安装 7-Zip、设置环境变量 GGZIP_7Z，"
                    "或将 7z.exe 放到程序同目录。");
}

ArchiveInfo SevenZipEngine::inspect(const std::wstring& archive,
                                    std::vector<Entry>* entries) const {
    ensure();
    const std::wstring real = resolveFirstVolume(archive);
    requireFile(real, "压缩包");
    std::string text;
    static std::atomic_bool noCancel{ false };
    proc::RunResult r = proc::run(exe_, { L"l", L"-slt", L"-sccUTF-8", real },
        [&](std::string_view c) { text.append(c); return true; }, noCancel);
    if (r.exitCode != 0) {
        std::string msg = "读取压缩包失败 (exit " + std::to_string(r.exitCode) + "): " + utf8(archive);
        if (!r.stderrTail.empty()) msg += "\n" + r.stderrTail;
        throw ArchiveError(msg);
    }
    Listing lst = parseListing(text);
    if (entries) *entries = std::move(lst.entries);
    return lst.info;
}

std::vector<Entry> SevenZipEngine::list(const std::wstring& archive) const {
    std::vector<Entry> es;
    inspect(archive, &es);
    return es;
}

void SevenZipEngine::runWithProgress(std::vector<std::wstring> args,
                                     const ProgressFn& onProgress,
                                     const std::atomic_bool& cancel,
                                     const std::string& what) const {
    args.push_back(L"-sccUTF-8");
    args.push_back(L"-bsp1");
    std::string cmdDebug = utf8(exe_);
    for (const auto& arg : args) { cmdDebug += " \"" + utf8(arg) + "\""; }
    std::string buf;
    double last = -1;
    proc::RunResult r = proc::run(exe_, args, [&](std::string_view c) {
        buf.append(c);
        if (buf.size() > 8192) buf.erase(0, buf.size() - 8192);
        if (onProgress) {
            int p = parsePercent(buf);
            if (p >= 0 && p != last) {
                last = p;
                onProgress({ (double)p, {} });
            }
        }
        return true;
    }, cancel);
    if (cancel.load()) return;   // 用户取消：由调用方处理，不作为错误
    if (r.exitCode != 0) {
        std::string msg = what + " 失败 (exit " + std::to_string(r.exitCode) + ")\n命令: " + cmdDebug;
        if (!r.stderrTail.empty()) {
            std::string t = r.stderrTail.size() > 2048
                          ? r.stderrTail.substr(r.stderrTail.size() - 2048) : r.stderrTail;
            msg += "\n" + t;
        }
        throw ArchiveError(msg);
    }
}

void SevenZipEngine::extract(const std::wstring& archive, const std::wstring& outDir,
                             const std::vector<std::wstring>& include,
                             const ExtractOptions& opt, const ProgressFn& onProgress,
                             const std::atomic_bool& cancel) const {
    ensure();
    const std::wstring real = resolveFirstVolume(archive);
    requireFile(real, "压缩包");
    std::vector<std::wstring> args{ L"x", real, L"-o" + outDir, L"-y" };
    if (!opt.password.empty()) args.push_back(L"-p" + opt.password);
    for (const auto& f : include) args.push_back(f);
    runWithProgress(std::move(args), onProgress, cancel, "解压");
}

void SevenZipEngine::create(const std::wstring& archive,
                            const std::vector<std::wstring>& inputs,
                            const CreateOptions& opt, const ProgressFn& onProgress,
                            const std::atomic_bool& cancel) const {
    ensure();
    if (inputs.empty()) throw ArchiveError("没有指定要压缩的文件或目录");

    if (isTarWrappedExt(archive)) {
        // 两段式：先在临时目录打 tar，再套外层压缩（gzip/bzip2/xz/zstd）
        wchar_t tmpRoot[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmpRoot);
        std::wstring tmpDir = std::wstring(tmpRoot) + L"ggzip_" +
                              std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(tmpDir.c_str(), nullptr);
        std::wstring tmpTar = tmpDir + L"\\tmp.tar";
        DeleteFileW(tmpTar.c_str());

        auto cleanup = [&]() {
            DeleteFileW(tmpTar.c_str());
            RemoveDirectoryW(tmpDir.c_str());
        };

        std::vector<std::wstring> a{ L"a", tmpTar, L"-ttar", L"-y" };
        if (opt.threads > 0) a.push_back(L"-mmt" + std::to_wstring(opt.threads));
        for (const auto& in : inputs) a.push_back(in);

        proc::RunResult r1 = proc::run(exe_, a,
            [](std::string_view) { return true; }, cancel);
        if (cancel.load()) { cleanup(); return; }
        if (r1.exitCode != 0) {
            cleanup();
            std::string msg = "压缩失败 (打包 tar 阶段, exit " + std::to_string(r1.exitCode) + ")";
            msg += "\n命令: " + utf8(exe_);
            for (const auto& arg : a) { msg += " \"" + utf8(arg) + "\""; }
            if (!r1.stderrTail.empty()) msg += "\n" + r1.stderrTail;
            throw ArchiveError(msg);
        }

        std::wstring e = lowerExtOf(archive);
        std::wstring t = (e == L".tar.bz2" || e == L".tbz" || e == L".tbz2") ? L"bzip2"
                       : (e == L".tar.xz" || e == L".txz") ? L"xz"
                       : (e == L".tar.zst" || e == L".tzst") ? L"zstd" : L"gzip";
        try {
            runWithProgress({ L"a", archive, L"-t" + t, L"-y", tmpTar },
                            onProgress, cancel, "压缩");
        } catch (...) { cleanup(); throw; }
        cleanup();
        return;
    }

    std::wstring type = archiveTypeFromExt(archive);
    if (type.empty())
        throw ArchiveError("不支持的压缩格式: " + utf8(archive) +
                    "（当前支持 7z / zip / tar / gz / bz2 / xz / zst / tar.gz / tar.bz2 / tar.xz 等）");

    // 单文件流格式（非 tar 包装）只支持单文件输入
    if ((type == L"gzip" || type == L"bzip2" || type == L"xz" || type == L"zstd") &&
        inputs.size() > 1)
        throw ArchiveError(utf8(type) + " 格式只支持单个文件；多文件请使用 .tar.gz / .tar.xz 等，或 7z / zip。");

    DeleteFileW(archive.c_str());   // create 语义 = 全新建压缩包
    if (!opt.volumeSize.empty()) {
        // 清理旧分卷（.001 .002 ...）
        for (int i = 1; i <= 9999; ++i) {
            wchar_t suffix[16];
            std::swprintf(suffix, 16, L".%03d", i);
            std::wstring v = archive + suffix;
            if (!fileExists(v)) break;
            DeleteFileW(v.c_str());
        }
    }

    std::vector<std::wstring> args{ L"a", archive, L"-t" + type, L"-y",
        L"-mx" + std::to_wstring(opt.level < 0 ? 5 : opt.level) };
    if (!opt.volumeSize.empty()) args.push_back(L"-v" + opt.volumeSize);
    if (opt.threads > 0) args.push_back(L"-mmt" + std::to_wstring(opt.threads));
    if (!opt.password.empty()) {
        args.push_back(L"-p" + opt.password);
        if (type == L"7z") args.push_back(L"-mhe=on");
    }
    for (const auto& in : inputs) args.push_back(in);
    runWithProgress(std::move(args), onProgress, cancel, "压缩");
}

void SevenZipEngine::test(const std::wstring& archive, const std::wstring& password,
                          const ProgressFn& onProgress,
                          const std::atomic_bool& cancel) const {
    ensure();
    const std::wstring real = resolveFirstVolume(archive);
    requireFile(real, "压缩包");
    std::vector<std::wstring> args{ L"t", real, L"-y" };
    if (!password.empty()) args.push_back(L"-p" + password);
    runWithProgress(std::move(args), onProgress, cancel, "完整性测试");
}

} // namespace ggzip

#pragma once
// 压缩引擎抽象接口。v0.1 实现基于 7z.exe 子进程；后续可增加 7z.dll（COM）实现。
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include "Entry.h"

namespace ggzip {

struct Progress {
    double percent = -1.0;        // 0~100；-1 表示未知
    std::wstring currentFile;
};

using ProgressFn = std::function<void(const Progress&)>;

struct ExtractOptions {
    std::wstring password;
};

struct CreateOptions {
    std::wstring password;   // 7z/zip 支持
    int level = 5;           // 0~9
    int threads = 0;         // 0 = 自动（交给引擎）
    std::wstring volumeSize; // 分卷大小，如 "100m"/"1g"；空 = 不分卷（产生 .001/.002...）
};

class IEngine {
public:
    virtual ~IEngine() = default;

    virtual bool available() const = 0;
    virtual std::wstring enginePath() const = 0;

    // 读取压缩包元信息与完整条目列表（一次遍历同时产出）
    virtual ArchiveInfo inspect(const std::wstring& archive,
                                std::vector<Entry>* entries) const = 0;
    virtual std::vector<Entry> list(const std::wstring& archive) const = 0;

    virtual void extract(const std::wstring& archive, const std::wstring& outDir,
                         const std::vector<std::wstring>& include,
                         const ExtractOptions& opt, const ProgressFn& onProgress,
                         const std::atomic_bool& cancel) const = 0;

    virtual void create(const std::wstring& archive,
                        const std::vector<std::wstring>& inputs,
                        const CreateOptions& opt, const ProgressFn& onProgress,
                        const std::atomic_bool& cancel) const = 0;

    virtual void test(const std::wstring& archive, const std::wstring& password,
                      const ProgressFn& onProgress,
                      const std::atomic_bool& cancel) const = 0;
};

} // namespace ggzip

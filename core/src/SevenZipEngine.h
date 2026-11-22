#pragma once
#include "ggzip/IEngine.h"

namespace ggzip {

// 基于 7z.exe（或 7za.exe）命令行的引擎实现。
class SevenZipEngine : public IEngine {
public:
    // sevenZipExe 为空表示未找到引擎
    explicit SevenZipEngine(std::wstring sevenZipExe) : exe_(std::move(sevenZipExe)) {}

    bool available() const override;
    std::wstring enginePath() const override;

    ArchiveInfo inspect(const std::wstring& archive,
                        std::vector<Entry>* entries) const override;
    std::vector<Entry> list(const std::wstring& archive) const override;

    void extract(const std::wstring& archive, const std::wstring& outDir,
                 const std::vector<std::wstring>& include,
                 const ExtractOptions& opt, const ProgressFn& onProgress,
                 const std::atomic_bool& cancel) const override;

    void create(const std::wstring& archive,
                const std::vector<std::wstring>& inputs,
                const CreateOptions& opt, const ProgressFn& onProgress,
                const std::atomic_bool& cancel) const override;

    void test(const std::wstring& archive, const std::wstring& password,
              const ProgressFn& onProgress,
              const std::atomic_bool& cancel) const override;

private:
    void ensure() const;
    void runWithProgress(std::vector<std::wstring> args, const ProgressFn& onProgress,
                         const std::atomic_bool& cancel, const std::string& what) const;

    std::wstring exe_;
};

} // namespace ggzip

#include "ggzip/SmartExtract.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/OperationError.h"
#include "ggzip/Diagnostics.h"
#include "FileTransaction.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace ggzip {

SmartDecision smartPlan(const std::vector<Entry>& entries) {
    std::set<std::wstring> topDirs;
    bool topLevelFiles = false;
    bool hasAny = false;

    for (const auto& e : entries) {
        std::wstring p = e.path;
        if (p.empty()) continue;
        hasAny = true;
        bool dir = e.isDir;
        if (!p.empty() && p.back() == L'/') { dir = true; p.pop_back(); }
        if (p.empty()) continue;
        auto slash = p.find(L'/');
        if (slash == std::wstring::npos) {
            if (dir) topDirs.insert(p);
            else topLevelFiles = true;
        } else {
            topDirs.insert(p.substr(0, slash));
        }
    }

    SmartDecision d;
    d.intoSubfolder = hasAny && !(topDirs.size() == 1 && !topLevelFiles);
    return d;
}

OperationSummary extractSmart(const IEngine& engine, const std::wstring& archive,
                          const std::wstring& outDir, bool smart,
                          const ExtractOptions& opt, const ProgressFn& onProgress,
                          const std::atomic_bool& cancel) {
    std::wstring target = outDir;
    if (smart) {
        auto entries = engine.list(archive, {opt.password, true, &cancel, opt.deadlineTick});
        auto d = smartPlan(entries);
        if (d.intoSubfolder) {
            // 以压缩包名（去掉全部扩展名）作为子文件夹名
            std::wstring name = fs::path(archive).filename().wstring();
            while (true) {
                auto dot = name.rfind(L'.');
                if (dot == std::wstring::npos || dot == 0) break;
                name = name.substr(0, dot);
            }
            if (name.empty()) name = L"extracted";
            target = outDir + L"\\" + dedupeFolderName(outDir, name);
        }
    }
    return engine.extract(archive, target, {}, opt, onProgress, cancel);
}

std::wstring dedupeFolderName(const std::wstring& parentDir,
                              const std::wstring& subfolderBase) {
    auto exists = [&parentDir](const std::wstring& name) {
        return files::identity(fs::path(parentDir) / name).exists;
    };
    std::wstring base = subfolderBase.empty() ? L"extracted" : subfolderBase;
    if (!exists(base)) return base;
    for (int i = 2; i < 1000; ++i) {
        std::wstring n = base + L"-" + std::to_wstring(i);
        if (!exists(n)) return n;
    }
    throw ConflictError(diagnostic("无法生成唯一的解压目录名称"));
}

} // namespace ggzip

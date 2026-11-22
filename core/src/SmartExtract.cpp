#include "ggzip/SmartExtract.h"
#include "ggzip/ArchiveError.h"
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

std::wstring extractSmart(const IEngine& engine, const std::wstring& archive,
                          const std::wstring& outDir, bool smart,
                          const ExtractOptions& opt, const ProgressFn& onProgress,
                          const std::atomic_bool& cancel) {
    std::wstring target = outDir;
    if (smart) {
        auto entries = engine.list(archive);
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
    std::error_code ec;
    fs::create_directories(fs::path(target), ec);

    engine.extract(archive, target, {}, opt, onProgress, cancel);
    if (cancel.load()) return target;

    // tar.* 两层格式：7z 一次只解一层，若解出的只有单个 .tar，再解一层
    if (isTarWrappedExt(archive)) {
        size_t count = 0;
        fs::path only;
        for (const auto& de : fs::directory_iterator(fs::path(target), ec)) {
            ++count;
            if (count == 1) only = de.path();
        }
        if (count == 1) {
            std::wstring ext = only.extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](wchar_t c) { return (wchar_t)std::towlower(c); });
            if (ext == L".tar") {
                engine.extract(only.wstring(), target, {}, opt, onProgress, cancel);
                if (!cancel.load()) fs::remove(only, ec);
            }
        }
    }
    return target;
}

std::wstring dedupeFolderName(const std::wstring& parentDir,
                              const std::wstring& subfolderBase) {
    auto exists = [&parentDir](const std::wstring& name) {
        DWORD a = GetFileAttributesW((parentDir + L"\\" + name).c_str());
        return a != INVALID_FILE_ATTRIBUTES;
    };
    std::wstring base = subfolderBase.empty() ? L"extracted" : subfolderBase;
    if (!exists(base)) return base;
    for (int i = 2; i < 1000; ++i) {
        std::wstring n = base + L"-" + std::to_wstring(i);
        if (!exists(n)) return n;
    }
    return base;
}

} // namespace ggzip

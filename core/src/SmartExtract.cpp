#include "ggzip/SmartExtract.h"
#include <windows.h>
#include <set>

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

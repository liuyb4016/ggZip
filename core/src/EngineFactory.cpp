#include "ggzip/EngineFactory.h"
#include "SevenZipEngine.h"
#include <windows.h>
#include <cstdlib>

namespace ggzip {
namespace {

bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring dirOf(const std::wstring& p) {
    auto pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : p.substr(0, pos);
}

} // namespace

std::optional<std::wstring> locateSevenZip() {
    std::vector<std::wstring> candidates;

    // 1) 环境变量
    if (const wchar_t* env = _wgetenv(L"GGZIP_7Z")) {
        if (*env) candidates.push_back(env);
    }

    // 2) 程序同目录（便携分发）
    wchar_t mod[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, mod, MAX_PATH)) {
        std::wstring dir = dirOf(mod);
        if (!dir.empty()) {
            candidates.push_back(dir + L"\\7z.exe");
            candidates.push_back(dir + L"\\7z\\7z.exe");
            candidates.push_back(dir + L"\\7za.exe");
            candidates.push_back(dir + L"\\7z\\7za.exe");
        }
    }

    // 3) PATH
    wchar_t pathBuf[MAX_PATH]{};
    if (SearchPathW(nullptr, L"7z.exe", nullptr, MAX_PATH, pathBuf, nullptr))
        candidates.push_back(pathBuf);

    // 4) 标准安装位置
    if (const wchar_t* pf = _wgetenv(L"ProgramFiles"); pf && *pf)
        candidates.push_back(std::wstring(pf) + L"\\7-Zip\\7z.exe");
    if (const wchar_t* pf86 = _wgetenv(L"ProgramFiles(x86)"); pf86 && *pf86)
        candidates.push_back(std::wstring(pf86) + L"\\7-Zip\\7z.exe");

    for (const auto& c : candidates)
        if (fileExists(c)) return c;
    return std::nullopt;
}

std::unique_ptr<IEngine> createEngine() {
    auto p = locateSevenZip();
    return std::make_unique<SevenZipEngine>(p.value_or(std::wstring()));
}

} // namespace ggzip

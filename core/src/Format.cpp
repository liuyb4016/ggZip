#include "ggzip/Format.h"
#include <algorithm>
#include <cwctype>

namespace ggzip {

std::wstring lowerExtOf(const std::wstring& p) {
    auto pos = p.rfind(L'.');
    if (pos == std::wstring::npos) return L"";
    std::wstring e = p.substr(pos);
    std::transform(e.begin(), e.end(), e.begin(),
                   [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return e;
}

std::wstring archiveTypeFromExt(const std::wstring& path) {
    std::wstring e = lowerExtOf(path);
    if (e == L".7z")  return L"7z";
    if (e == L".zip") return L"zip";
    if (e == L".tar") return L"tar";
    if (isTarWrappedExt(path)) return L"";
    if (e == L".gz")  return L"gzip";
    if (e == L".bz2") return L"bzip2";
    if (e == L".xz")  return L"xz";
    if (e == L".zst") return L"zstd";
    return L"";
}

bool isTarWrappedExt(const std::wstring& path) {
    // 注意：要检查完整结尾（如 ".tar.gz"），而不是最后一个扩展名（".gz"）；大小写不敏感
    std::wstring lower(path);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t c) { return (wchar_t)std::towlower(c); });
    std::wstring_view lv(lower);
    auto endsWith = [&lv](std::wstring_view s) {
        return lv.size() >= s.size() && lv.substr(lv.size() - s.size()) == s;
    };
    return endsWith(L".tar.gz") || endsWith(L".tgz") ||
           endsWith(L".tar.bz2") || endsWith(L".tbz") || endsWith(L".tbz2") ||
           endsWith(L".tar.xz") || endsWith(L".txz") ||
           endsWith(L".tar.zst") || endsWith(L".tzst");
}

} // namespace ggzip

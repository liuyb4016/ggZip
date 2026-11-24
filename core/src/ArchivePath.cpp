#include "ggzip/ArchivePath.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/Diagnostics.h"
#include <algorithm>
#include <cwctype>

namespace ggzip {
namespace {

bool reservedName(std::wstring name) {
    name = name.substr(0, name.find(L'.'));
    while (!name.empty() && name.back() == L' ') name.pop_back();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](wchar_t character) { return static_cast<wchar_t>(std::towupper(character)); });
    if (name == L"CON" || name == L"PRN" || name == L"AUX" || name == L"NUL" ||
        name == L"CONIN$" || name == L"CONOUT$" || name == L"CLOCK$") return true;
    if (name.size() != 4 || (name.substr(0, 3) != L"COM" && name.substr(0, 3) != L"LPT"))
        return false;
    const wchar_t suffix = name.back();
    return (suffix >= L'1' && suffix <= L'9') || suffix == L'\u00b9' ||
           suffix == L'\u00b2' || suffix == L'\u00b3';
}

}

std::wstring validatedPreviewPath(std::wstring_view path) {
    std::wstring normalized(path);
    std::replace(normalized.begin(), normalized.end(), L'\\', L'/');
    if (normalized.empty() || normalized.front() == L'/' || normalized.back() == L'/')
        throw ArchiveError(diagnostic("不安全的预览条目路径"));
    for (const wchar_t character : normalized) {
        if (character < 32 || character == 127 ||
            std::wstring_view(L":<>\"|?*").find(character) != std::wstring_view::npos)
            throw ArchiveError(diagnostic("预览条目包含不允许的路径字符"));
    }
    std::size_t start = 0;
    for (;;) {
        const std::size_t separator = normalized.find(L'/', start);
        const std::wstring component = normalized.substr(start, separator - start);
        if (component.empty() || component == L"." || component == L".." ||
            component.back() == L'.' || component.back() == L' ' || reservedName(component))
            throw ArchiveError(diagnostic("预览条目包含不安全的路径部分"));
        if (separator == std::wstring::npos) break;
        start = separator + 1;
    }
    return normalized;
}

}

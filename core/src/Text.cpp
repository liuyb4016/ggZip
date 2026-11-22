#include "ggzip/Text.h"
#include <windows.h>

namespace ggzip {

std::string utf8FromWide(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                            out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring wideFromUtf8(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

} // namespace ggzip

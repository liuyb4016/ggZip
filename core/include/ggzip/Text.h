#pragma once
// 窄字符串统一使用 UTF-8 编码；Windows 路径与界面文本使用宽字符。
#include <string>
#include <string_view>

namespace ggzip {

std::string utf8FromWide(const std::wstring& s);
std::wstring wideFromUtf8(std::string_view s);

inline std::string utf8(const std::wstring& s) { return utf8FromWide(s); }

} // namespace ggzip

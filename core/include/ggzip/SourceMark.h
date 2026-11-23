#pragma once
#include <string>

namespace ggzip {

unsigned sourceZone(const std::wstring& path);
void applySourceZone(const std::wstring& path, unsigned zone);

}

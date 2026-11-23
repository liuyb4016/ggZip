#pragma once
#include <string>

namespace ggzip {

void registerFileAssociations(const std::wstring& applicationPath, bool notifyShell = true);
void unregisterFileAssociations(const std::wstring& applicationPath, bool notifyShell = true);

}

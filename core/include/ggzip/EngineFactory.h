#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "IEngine.h"

namespace ggzip {

// 定位 7z.exe（按优先级）：
//   1) 环境变量 GGZIP_7Z 指向的 7z.exe / 7za.exe
//   2) 主程序所在目录及其 7z\ 子目录（便携分发）
//   3) PATH
//   4) %ProgramFiles%\7-Zip\7z.exe 与 %ProgramFiles(x86)%\7-Zip\7z.exe
std::optional<std::wstring> locateSevenZip();

// 返回引擎实例；未找到 7z.exe 时 available() 为 false（各操作会抛 Error 说明）。
std::unique_ptr<IEngine> createEngine();

} // namespace ggzip

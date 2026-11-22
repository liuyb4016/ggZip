// ggZip Shell 扩展 —— 定位并启动 ggz.exe
#pragma once

#include <string>
#include <vector>

namespace ggshell {

// 查找 ggz.exe 依次尝试：
//   1) 环境变量 GGZIP_GGZ（完整路径，测试/调试用）
//   2) 注册表 HKCU\Software\ggZip\GgzPath（安装器写入）
//   3) 本 DLL 同目录下的 ggz.exe
//   4) 兜底 "ggz.exe"，由 CreateProcess 按 PATH 搜索
std::wstring findGgz();

// 在独立线程中逐条执行命令（互不并行），失败弹错误框；
// 设置了环境变量 GGZIP_SHELL_DRYRUN=<文件> 时只把命令行追加到该文件，不启动进程（测试钩子）。
void launchAsync(const std::wstring& exe,
                 const std::vector<std::vector<std::wstring>>& commands);

} // namespace ggshell

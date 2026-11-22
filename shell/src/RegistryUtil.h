// ggZip Shell 扩展 —— 注册/注销（HKCU，无需管理员）
#pragma once

namespace ggshell {

// 写入 HKCU\Software\Classes：CLSID + *、Directory、Directory\Background 三类处理器
bool registerServer();

// 删除上述键（尽力而为，任何一步失败都继续清理其余）
bool unregisterServer();

} // namespace ggshell

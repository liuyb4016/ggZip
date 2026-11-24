# AGENTS.md — ggZip 项目指引

面向在本仓库工作的 AI 编码助手。修改代码前先读本文件和 `README.md`、`ROADMAP.md`。

## 项目定位与状态

Windows 平台压缩软件，目标对标 Bandizip：压缩包浏览器、智能解压、Shell 右键集成、加密、分卷、SFX。
- 当前版本见 `VERSION`（现为 `0.4.0`），**尚未正式发布**；发布标签须与 `VERSION` 一致（`v<版本号>`）。
- 代码以 MIT 发布；内置 7-Zip 26.06 引擎保留 LGPL 2.1-or-later / BSD / unRAR 限制，不适用 MIT。
- 区分三种状态：**代码已实现** ≠ **自动化已验证** ≠ **正式发布**。文档中对 v0.5/v0.6/v0.7 功能的描述以 `ROADMAP.md` 为准，不要据此宣称已发布。

## 目录结构

```
core/   纯 C++20 核心库（无 UI 依赖）：IEngine 抽象、7z.exe 子进程驱动、进程管理、
        智能解压、事务与文件句柄操作、私有目录/ACL、安全路径、分卷、密码库(DPAPI)、诊断
cli/    ggz 命令行工具（唯一 CLI 源文件 main.cpp）
gui/    Qt 6 Widgets GUI：浏览器、对话框、后台任务桥、预览会话/解码、任务队列、语言管理
shell/  Windows 右键菜单 COM 扩展（独立 DLL，不依赖 Qt/core）+ 自动化测试宿主
tests/  CTest 用例（核心/GUI/安全/事务/分卷/诊断/性能）
packaging/  引擎获取、便携包与 Inno Setup 安装器、签名、文件系统/安装/性能验证脚本
.github/workflows/  ci / gui / nightly / package / release 五条工作流
```

约束：`core` 只能依赖 C++20 标准库与 Win32，不得引入 Qt 或 UI 依赖——GUI、CLI、Shell 三层都复用它。

## 构建与测试

需要 CMake ≥ 3.20、C++20 编译器；GUI 需 Qt 6.9.3 MSVC x64（含 Qt Image Formats）。

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
GUI 加 `-DGGZ_BUILD_GUI=ON -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/msvc2022_64`；Shell 为独立工程 `cmake -S shell -B build-shell -A x64`。

测试前必须准备固定版本引擎并清除对系统 7-Zip 的依赖：
```powershell
$engine = .\packaging\prepare-engine.ps1
$env:GGZIP_7Z = Join-Path $engine.Directory '7z.exe'
ctest --test-dir build --output-on-failure
```
打包：`.\packaging\build.ps1 -QtDir ... -IsccPath ...`，产物在 `out/packages/`（便携包 ZIP、安装器、SHA256 清单）。
本地 `out/`、`tools/`、`build-*/` 均为生成物且已在 `.gitignore` 中，不要提交，也不要删除他人的验证目录。

## 关键设计约定（改动时务必遵守）

1. **引擎抽象**：所有压缩操作走 `IEngine`（`core/include/ggzip/IEngine.h`），进度、取消（`std::atomic_bool`）、冲突回调统一；引擎定位顺序为 `GGZIP_7Z` → 程序旁 `7z/7z.exe` → `PATH` → 标准安装位置。禁止把第三方引擎路径或系统原始诊断写死。
2. **安全边界**：预览使用独立会话 + 逐次临时目录，流式读取条目字节，拒绝越界/绝对/UNC/ADS/设备名/符号及硬链接/重解析点/重复条目；非文本图片的系统打开须明确确认；图片在受 Job Object 内存与时间限制的辅助进程解码。改动 `PreviewSession`、`ArchivePath`、`ProcessUtil` 时保持这些不变量。
3. **事务与冲突**：创建/分卷/解压/编辑均先写同卷独立暂存目录，校验大小与 CRC 后按文件句柄身份提交，失败回滚并保留备份目录；事务目录创建时设置仅当前用户/SYSTEM 的 ACL。GUI 逐项询问覆盖/跳过/重命名，CLI 默认拒绝（`--on-conflict error|overwrite|skip|rename`）。无 ACL 卷回退到当前用户本地私有暂存，不能保留 MOTW 的目标明确拒绝。
4. **诊断与 i18n**：自有文案用 `diagnostic("原文键")` 查表后再拼接参数，不对完整错误、路径、用户内容或第三方引擎输出做前缀/子串翻译；字典编入核心库。CLI `--lang zh_CN|en`，稳定错误码与退出码（冲突 4、取消 3、部分失败 1、显式帮助 0）不随语言变化。调用方在启动后台任务前确定语言并保持一致。
5. **密码库**：DPAPI 当前用户作用域，存于 `%APPDATA%\ggZip\passwords.dat`；只有用户明确选择才保存，切换压缩包清除会话密码，密码不写入通用设置，诊断不输出密码或完整命令行。
6. **编辑范围**：ZIP/7z 单卷支持添加/删除/重命名；其他格式与分卷只读。批量添加走受控 UTF-8 列表一次更新，不逐文件重写固实包，不扫入自身操作目录或原包。

## 开发流程约定

- 功能实现后补对应自动化用例，运行相关 CTest 与分发自检；涉及 Shell/关联/安装时还需真实 Windows 桌面验收，宿主测试不能替代 Explorer 实际操作。
- 新增功能同步更新 `README.md` 与 `ROADMAP.md`；版本变更同步 `VERSION`。文档要如实区分"本地实现/自动化验证/正式发布"。
- **未经用户确认，不自动提交、推送、创建发布标签、安装程序、修改默认应用、注册菜单或导入证书。** 标签工作流默认发布未签名 prerelease；签名只能用用户证书库中已有证书，不入库私钥/PFX。
- 涉及安全边界（路径、ACL、事务、取消、MOTW、编码）的改动优先补回归测试，再动实现逻辑。

## 发布前未完成事项（详见 ROADMAP.md P0/REL-01）

- 真实 Win10 1809+/Win11 x64 安装、升级、卸载、便携启动与中文/空格路径、文件关联、右键菜单的人工验收。
- FAT32/exFAT 与真实磁盘满测试脚本的远端 CI 运行；持续目录竞争、跨用户权限、系统级低内存/断电恢复压力验收。
- RAR 真实分卷/加密/缺尾卷、超过 4 GiB 单文件、十万小文件的完整回归与性能测量。
- 可信签名方案、真实 Explorer 验收后 Win11 新式菜单的生产注册；中英文完整桌面布局验收。

上述项完成并经用户确认后，才可进入正式发布流程。

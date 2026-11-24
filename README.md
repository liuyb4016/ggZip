# ggZip

Windows 平台压缩软件，目标对标 [Bandizip](https://en.bandisoft.com/bandizip)：压缩包浏览器、智能解压、右键 Shell 集成、加密等。

## 已实现

### 下一版本 v0.4.0 —— 文件关联 / 安装包 / 完整发布（尚未发布）

- **事务与冲突保护（本地开发中）**：GUI 逐项询问覆盖、跳过或重命名，可应用于本次同类冲突；
  CLI 默认拒绝冲突，使用 `--on-conflict error|overwrite|skip|rename` 明确选择。
  创建及分卷先写同卷独立暂存目录，成功后替换整组；提交失败回滚，无法恢复时保留备份并报告目录。
  常规解压先验证安全成员并暂存，按大小/CRC 核验后发布；失败或取消只保留可验证的完整新文件，
  不覆盖既存文件，也不发布缺少可靠校验和的中断结果
- **预览与来源保护（本地开发中）**：非文本/图片的系统打开必须确认；保留压缩包的 MOTW 区域标记。
  预览默认限制 200 MiB、6400 万像素、30 秒；图片在受 Job Object 内存/时间限制的辅助进程解码。
  设置可明确提高预览上限；完整解压进行空间预检和实际输出监控，拒绝既存重解析点与文件/目录冲突
- **私有暂存（本地开发中）**：预览及事务目录创建时直接设置受保护 ACL，只授予当前用户和 SYSTEM；
  不继承共享父目录的 Everyone 权限。操作期间持有目录锁，提交/回滚以目录句柄定位目标，
  清理通过已验证身份的文件/目录句柄执行，不递归删除未知内容
- **校验取消**：CRC 校验使用锁定的只读句柄，分块响应取消；提交绑定校验时的文件身份，
  被改动的数据不能作为已验证结果发布
- **文件关联**：GUI「设置-文件关联」可注册/移除「打开方式」候选，并打开 Windows 默认应用设置；
  仅写入当前用户注册表，不覆盖现有扩展名默认值或 Windows `UserChoice`。
  用户自行选为默认后，双击压缩包会打开 GUI，并显示 ggZip 图标
- **安全清理**：卸载只删除本执行文件路径拥有的关联；其他安装副本、其他程序关联及密码库不受影响；
  新增 27 项注册表隔离测试，覆盖中文路径、带空格路径、重复注册、跨副本清理与默认应用保护
- **安装包**：Inno Setup，支持 Windows 10 1809+ / Windows 11 x64，当前用户安装，无需管理员；
  可选桌面快捷方式、文件关联和经典右键菜单；
  内置 7-Zip 26.06 引擎，支持同目录升级和卸载清理，不需要用户另装 7-Zip
- **完整便携包**：GUI、CLI、Shell DLL、Qt 插件、x64 Visual C++ 运行库、
  7-Zip 引擎及 SFX 模块、第三方许可与对应 7-Zip 源码一起打包；无需首次运行联网下载
- **内置引擎验证**：清除 `GGZIP_7Z`、限制 `PATH` 仅包含 Windows 系统目录，
  验证实际选用包内引擎，并测试中文路径、ZIP/加密 7z/SFX 回读和分卷解压
- **安全预览**：每个窗口/每次预览使用独立临时目录；只从引擎读取选中条目的字节流，
  不按包内路径直接落盘。拒绝越界、绝对路径、ADS、设备名、符号/硬链接或重解析点及重复条目，
  提取失败或取消不会显示旧缓存；只删除本实例记录的文件及空目录，不递归清理共享缓存
- **预览安全回归**：新增 72 项路径/流式提取检查和 26 项会话/文件所有权检查，
  覆盖中文、参数边界、密码、损坏包、伪造元信息、符号/硬链接、NTFS 目录连接及跨实例清理
- **发布流程**：GUI CI、nightly 和正式版本复用同一构建/测试/打包流程，
  产出 ZIP、安装器和 SHA256 校验和；正式发布标签必须与 `VERSION` 一致

### v0.5 日常体验功能（本地开发中，未创建版本）

- **选中解压**：多选文件/目录递归解压，保留包内目录结构，复用整体解压的冲突与取消保护
- **完整性测试**：后台测试、取消与密码重试；不修改原压缩包
- **搜索与设置**：当前目录过滤/全包名称搜索；补充隐式目录导航；最近打开历史、输出目录、
  压缩级别及窗口布局保存，历史可清除，密码不写入通用设置
- **加密浏览与后台预览**：加密头部列表、加密条目预览和测试使用当前压缩包会话密码；
  错误密码可重试，切换压缩包清除会话；只有明确选择时才保存到 DPAPI 密码库
- **图片分发回归**：PNG/JPEG/GIF/BMP/WebP/ICO 实际编码/解码检查；
  CI 安装匹配的 Qt Image Formats 开发模块，最终分发包包含 WebP 插件，用户无需另装组件

这些开发功能尚未正式发布，远端 CI 与 Windows 10/11 完整桌面验收结果仍需另行验证。

### v0.6 / v0.7 后续功能（本地开发中，未创建版本）

- **中英文 GUI**：设置选择简体中文或 English，重启生效；英文文案嵌入资源，
  分发包含标准 Qt 中文对话框翻译。密码/冲突错误有稳定错误代码，核心详细诊断暂仍保留中文
- **串行任务队列**：批量压缩或解压，单项取消/整队取消，失败后停止或继续；
  每个压缩包使用独立、去重的输出目录，按任务展示进度和结果；多文件拖入窗口可进入批量界面
- **ZIP/7z 编辑**：添加文件/目录（包括空目录）、删除选中条目、重命名文件/目录；
  支持密码，其他格式及分卷只读。修改快照后验证未操作条目、完整性和新增内容，再事务替换原包
- **手动版本检查**：仅用户点击时查询官方 GitHub 稳定发布，显示说明并提供发布页入口；
  有来源/大小/超时检查，不静默下载、安装或执行更新程序
- **签名钩子**：打包可使用当前用户证书库中的明确证书指纹与 SignTool，对自有程序和安装器签名并校验；
  不导入证书，不将私钥或 PFX 存入仓库。默认仍是未签名测试包，标签工作流默认发布 prerelease
- **Win11 菜单原型**：独立 `IExplorerCommand`、直接 COM 宿主回归和外部位置身份包构建/部署工具；
  默认不注册，不随普通未签名安装器启用。可信签名、真实 Explorer 验收与 Inno 集成仍是发布门槛，
  详见 `packaging/modern/README.md`
- **异常/体验回归**：补 GUI 选中解压、加密浏览密码重试、加密预览/取消、被占用目标、
  损坏包完整新文件保留、缺失卷、GBK 与截断编码、大列表和命令行长度限制测试
- **显示边界**：归档路径和错误信息按纯文本显示；图片按内容识别后仅允许声明的栅格格式，
  不能用伪装 PNG 的 SVG 绕过图片预览流程

### v0.2.0 —— Qt GUI 压缩包浏览器（M1）

- **主窗口**：菜单/工具栏/拖拽打开压缩包；条目表格（名称/大小/压缩后/类型，目录排前）；UTF-8 中文路径与文件名正常显示
- **解压到 / 智能解压到**：智能解压为 Bandizip 行为（单根目录直接解压，否则自动建子文件夹）；支持密码
- **新建压缩包**：多选文件与文件夹、输出格式按扩展名（7z/zip/tar/gz/bz2/xz/zst/tar.gz 等）、压缩级别 1-9、密码加密
- 压缩级别现可选 0～9（0 为仅存储），窗口布局及常用压缩级别可记忆
- **后台执行**：操作在独立线程运行，进度对话框可取消；结果/错误经队列信号回传 GUI 线程
- 基于 Qt6 Widgets（6.9.3 LTS 验证通过），MSVC 与 MinGW 双工具链编译回归通过

### v0.3.0 —— M3：密码管理器 / 分卷 / SFX

- **密码管理器**：密码以 DPAPI（当前用户作用域）加密存储于 `%APPDATA%\ggZip\passwords.dat`；
  CLI `ggz pass list / add <标签> -p <密码> / remove <标签>`；
  GUI 「设置-密码管理器」增删管理，解压/创建对话框提供「从密码库」下拉一键填充
- **分卷压缩**：创建对话框选择分卷大小（10m/50m/100m/500m/1g，可自定义），7z 分卷 `.001/.002...`；
  解压/列表/测试直接给基础名（`a.7z`）即可——引擎自动解析首卷
- **SFX 自解压包**：保存为 `.exe`（7z `-sfx`）；分发包已包含完整引擎和 SFX 模块，
  无需额外安装；若自行换用便携 7za，则不支持 SFX
- **文本预览**：双击 txt/md/log/代码等文本条目弹窗预览（等宽字体、前 100KB 截断提示），
  BOM/UTF-8/GBK 自动识别

### v0.2.1 —— 浏览器增强

- **目录导航**：双击目录进入；"上级"按钮 / Alt+Up 返回；导航栏显示当前路径
- **图片预览**：双击图片条目（png/jpg/jpeg/gif/bmp/webp/ico）安全提取到临时目录后后台解码；
  其他文件经风险确认后用系统默认程序打开（临时文件退出时按所有权清理）
- **列排序**：点击表头按名称/大小/压缩后/类型升降序（目录始终排前）
- **选中统计**：状态栏实时显示已选条目数与合计大小

### v0.1.0 —— 核心引擎 + CLI

- **引擎**：基于内置 `7z.exe` 子进程，无需另外安装 7-Zip。
  定位顺序：显式环境变量 `GGZIP_7Z` → 程序旁的 `7z\7z.exe` → 同目录旧版引擎 → `PATH` → 标准安装位置
- **创建**：7z / zip / tar / gz / bz2 / xz / zst / tar.gz / tar.bz2 / tar.xz（tar.* 自动两段式打包）
- **加密**：7z 密码加密（AES-256 + 头部加密 `-mhe`）、zip 密码加密
- **解压/列表**：7z 引擎支持的 40+ 格式（zip、7z、rar、tar、gz、iso、cab、wim、msi 等）
- **智能解压**：包内单一根目录时直接解压，否则自动创建以压缩包名命名的子文件夹（Bandizip 行为）
- **tar.\* 双层自动解包**：解压 tar.gz / tar.bz2 / tar.xz 一步到位
- **进度显示 + Ctrl+C 取消**；全程 UTF-8，中文路径 / 中文文件名无乱码
- 冒烟测试覆盖：创建/列表/解压回读（zip/7z/tar/tar.gz、中文路径、二进制文件）、加密解压、智能解压决策

## 构建

需要：CMake ≥ 3.20，C++20 编译器（MSVC 或 MinGW-w64）。

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物：`build\ggz.exe`（命令行）、`build\ggzip_smoke.exe`（测试）。

### 构建 GUI（可选，需要 Qt6）

```bat
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 ^
      -DGGZ_BUILD_GUI=ON -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/msvc2022_64
cmake --build build-msvc --config Release
```

运行时需 Qt DLL 在 PATH 中：

```bat
set PATH=C:\Qt\6.9.3\msvc2022_64\bin;%PATH%
build-msvc\gui\Release\ggzip_gui.exe [可选：压缩包路径]
```

### 构建 Shell 右键菜单扩展（可选，独立工程）

```bat
cmake -S shell -B build-shell -A x64
cmake --build build-shell --config Release
```

产物 `build-shell\Release\ggzip_shell.dll`（MSVC，静态链接 C++ 运行库，不依赖 Qt/core）。
因此 Explorer 加载扩展不依赖系统另行安装 Visual C++ 运行库。
注册到当前用户（无需管理员；Win11 需在右键「显示更多选项」中可见）：

```bat
regsvr32 build-shell\Release\ggzip_shell.dll   &:: 注销加 /u
```

菜单项：智能解压 / 解压到当前文件夹 / 解压到同名文件夹（选中压缩包时），添加到 `<名>.7z` / `.zip`（选中文件或文件夹空白处时）。
默认优先启动相邻 GUI，支持交互冲突确认；显式设置 `GGZIP_GGZ` 时使用 CLI。
CLI 定位顺序：环境变量 `GGZIP_GGZ` → 注册表 `HKCU\Software\ggZip\GgzPath` → DLL 同目录 → `PATH`。

自动化测试（不经 Explorer，27 项检查；干跑验证命令行，`--e2e` 走真实 ggz 全链路）：

```bat
build-shell\Release\shell_host_test.exe build-shell\Release\ggzip_shell.dll
set GGZIP_7Z=%CD%\tools\7zip\26.06\engine\7z.exe
build-shell\Release\shell_host_test.exe build-shell\Release\ggzip_shell.dll build\cli\ggz.exe --e2e
```

## 运行测试

对于源码的独立 CMake 构建，先准备固定版本引擎，无需安装系统 7-Zip：

```powershell
$engine = .\packaging\prepare-engine.ps1
$env:GGZIP_7Z = Join-Path $engine.Directory '7z.exe'
ctest --test-dir build --output-on-failure
```

支持 CTest（核心冒烟、文件关联隔离、预览路径/流式安全、事务保护与进程资源限制测试；
启用 GUI 构建时还包含 Qt 预览会话、模型/搜索、图片解码及后台取消回归）：

```bat
ctest --test-dir build --output-on-failure
ctest --test-dir build-msvc -C Release --output-on-failure
```

### 构建完整便携包与安装包

需要 Windows x64、MSVC、CMake/CTest、Qt 6.9.3 MSVC x64（含 Qt Image Formats 开发模块）、PowerShell 7、
Inno Setup ≥ 6.3。CMake/CTest 需在 `PATH` 中。
打包脚本自动获取并缓存 SHA256 固定的官方 7-Zip 引擎、SFX 模块与对应源码，
不执行 7-Zip 安装程序；同时获取匹配 Qt 版本的 LGPL/GPL 许可文本。

```powershell
.\packaging\build.ps1 -QtDir 'C:\Qt\6.9.3\msvc2022_64' `
    -IsccPath 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
```

默认输出到 `out/packages/`：

- `ggZip-v0.4.0-win64.zip`：解压后直接运行 `ggzip_gui.exe`
- `ggZip-v0.4.0-win64-setup.exe`：当前用户安装器
- `ggZip-v0.4.0-win64-SHA256SUMS.txt`：便携包及安装器校验和

脚本不会覆盖已有分发目录或安装器；重跑时指定新的 `-OutputDir` 或 `-PackageName`。
使用 `-SkipBuild` 可复用已构建的 Release 产物，但仍会执行 CTest 和分发自检。
默认自动定位 MSVC x64 可再分发 DLL；也可用 `-RuntimeDir` 指定目录。
安装器保留 `%APPDATA%\ggZip` 下的密码数据，且不会自行改变默认应用。
7-Zip 仅作为内部组件放到 `{app}\7z`，不会额外安装其文件管理器、右键菜单或卸载程序；
不会修改系统原有 7-Zip。便携包也已自带引擎，用户无需另外安装或配置。
引擎版本、下载地址和逐文件校验和统一记录在 `packaging/7zip.json`；
升级引擎需同步更新官方二进制、对应源码和许可文件的校验信息。

GUI CI 上传完整分发产物。Nightly 保持每日北京时间 05:00、有新提交才发布的行为，
现在也包含 GUI 和安装器。正式发布由与 `VERSION` 相同的 `v<版本号>` 标签触发。
默认标签发布目前为未签名测试 prerelease，而不是自动稳定发布。
CI 使用 SHA256 固定的 `Tools.InnoSetup` 6.7.3 NuGet 工具包，并验证编译器签名；
它仅用于构建，不随 ggZip 分发。

Shell 宿主测试会注册/注销经典右键菜单，只应在无现有 ggZip Shell 注册的测试环境运行；
打包脚本需显式传入 `-RunShellTests` 才会执行。安装、升级、卸载集成测试由
`packaging/test-installer.ps1` 在一次性 GitHub Actions runner 上执行，
不在本机自动安装 ggZip 或修改本机默认应用。安装及升级后，CI 还会在清除引擎环境变量、
不使用系统 7-Zip 的条件下执行完整内置引擎回归。

可选签名构建：向 `packaging/build.ps1` 同时提供 `-SignCertificateThumbprint` 和
`-SignToolPath`；证书必须已经位于当前用户证书库且有私钥，签名后验证可信链与证书指纹。
只签 ggZip 自有程序、Shell DLL 和安装器，不修改官方内置 7-Zip 二进制。
这个构建钩子不创建或导入证书，不传递 PFX 密码；证书/签名服务须由发布者另行提供。

## CLI 用法

```bat
ggz list archive.zip
ggz list secret.7z -p MyPassword
ggz extract archive.7z -o D:\out
ggz extract archive.7z -o D:\out --on-conflict rename
ggz smart-extract tool.tar.gz -o D:\tools
ggz batch-extract first.zip second.7z -o D:\batch --continue-on-error
ggz create backup.7z D:\docs --level 7 --threads 8
ggz create secret.zip D:\docs -p MyPassword
ggz create split.7z D:\docs --volume 100m
ggz test archive.zip
ggz add archive.zip new.txt --on-conflict rename
ggz delete archive.zip folder/file.txt
ggz rename archive.7z old-folder new-folder -p MyPassword
ggz pass list
ggz pass add backup.7z -p MyPassword
ggz pass remove backup.7z
ggz where
ggz --lang en list archive.zip
ggz extract archive.zip -o output --lang en
```

CLI 默认使用简体中文；`--lang zh_CN|en` 可放在命令前后，也支持 `--lang=en`。
帮助、状态、自有核心/预览错误详情随语言切换，普通失败、冲突与密码错误的稳定代码不变。
显式 `--help` 返回退出码 0。`--` 后的参数按字面量处理，例如：
`ggz --lang en delete archive.zip -- --lang en` 删除名为 `--lang` 和 `en` 的条目，
不会把条目名或 `-p` / `-o` 的值误当作语言选项。
路径、文件名、第三方引擎及系统原始诊断不做替换，也不会为翻译而输出密码或完整命令行。
GUI 使用保存的界面语言同步核心后台错误详情；语言资源加载失败时不破坏已加载的翻译。

CLI 冲突默认退出码 4；取消退出码 3；部分操作失败退出码 1，报告已保留的完整文件数量。
创建取消保留原包。中断解压不能保证恢复每个条目：只有大小和 CRC 均通过核验的完整新文件才保留。
取消校验时不为了抢救更多文件而继续长时间计算；只发布取消前已完成核验的完整新文件。
整包暂存会额外占用压缩包快照及解码空间，不使用逐条重新解码固实包的方法。
完整暂存包含未选中条目，但目录与明文文件必须使用私有 ACL；
在支持 ACL 的目标卷上，最终发布的新文件仍保留私有权限，不自动扩大为共享目录的读权限。
对共享目标的原位重解析点变化已有拒绝回归，持续竞争/低磁盘压力仍需发布前专项验收；
共享目录的其他使用者可能删除或移动其内容，敏感数据仍应使用私有目标目录。
FAT/exFAT 等无 ACL 目标的操作会将完整快照和解码暂存放到当前用户的本地应用数据目录，
且仍须验证受保护 ACL；没有可验证的私有暂存位置时拒绝操作，不降级为共享明文暂存。
跨卷时只将已选中、已校验的结果复制到目标卷的独立提交目录；复制句柄拒绝共享访问，
完整字节比对和保存成功后才替换目标，复制失败或取消不移走旧文件。
目标卷还需容纳所选结果的提交副本。无 ACL 文件系统无法提供最终文件的用户权限隔离；
异常断电可能留下所选文件的临时副本，应使用私有 NTFS 目标保存敏感结果。
无 ADS 的目标不能保留已有 MOTW，因此带来源标记的内容会在发布前明确拒绝，
应选择支持来源标记的目标，不建议移除来源标记来绕过保护。
上述 FAT32/exFAT 与磁盘满集成用例已接入隔离 CI，但尚待最新代码远端运行验证。
事务快照已覆盖 `.001/.002…`、RAR `.part1.rar` / `.r00`、ZIP `.z01` 等卷组，
支持从后续卷定位主卷、拒绝重复编号及中间缺卷，并汇总各卷的来源标记。
多盘 ZIP 的浏览、测试、解压和预览已有真实格式回归；RAR 的真实分卷、加密和缺尾卷仍需扩大验收。
批量解压同时选择同一卷组的多个文件时只排入一个任务。
编辑原包仅限单文件 ZIP/7z，失败或取消不会提交快照。`delete` 的目录参数包含全部后代；
`add` 支持文件/目录，默认保留输入的文件/目录名，GUI 添加到当前浏览目录。
批量添加通过受控 UTF-8 列表一次更新，避免逐文件重写固实包；不会扫描进自己的操作目录或原包。
程序声明长路径感知，不修改系统长路径策略；超长命令行会在启动引擎前明确拒绝。
发布前验收步骤与失败恢复说明见 [验证清单](packaging/VALIDATION.md)。
有预算的大文件、小文件和图片测量及报告字段说明见 [性能测量](packaging/PERFORMANCE.md)；
进程组内的内存拒绝测试不等于整机低内存或断电验收。

## 架构

```
core/   纯 C++20 核心库（无 UI 依赖）：引擎抽象 IEngine、7z.exe 驱动、
        进程管理、智能解压（决策 + 执行 extractSmart）、格式识别
cli/    ggz 命令行工具
gui/    Qt Widgets GUI：压缩包浏览器、解压/压缩对话框、后台任务桥（v0.2）
shell/  Windows 右键菜单 COM 扩展（独立 DLL，不依赖 Qt/core）：经典菜单 +
        自动化测试宿主；执行体为 ggz.exe（干跑测试钩子 GGZIP_SHELL_DRYRUN）
```

核心库只依赖 C++20 标准库与 Win32，便于后续被 GUI、Shell 扩展、CLI 复用。
`ggzip/Diagnostics.h` 提供应用级诊断语言设置；调用者应在启动后台任务前确定语言并在任务期间保持一致。
自有文案使用 `diagnostic("原文键")` 查表后再拼接参数，不对完整错误、文件路径或用户内容进行前缀/子串翻译。
字典直接编入核心库，不依赖额外翻译程序或外部语言文件。

## 路线图

详细开发清单、优先级、验收标准与版本建议见 [ROADMAP.md](ROADMAP.md)。

- [x] M0/v0.1 —— 核心引擎 + CLI + 智能解压 + 冒烟测试
- [x] M1/v0.2 —— Qt GUI：压缩包浏览器（列表/解压/压缩/智能解压/进度取消）
- [x] M2 —— Windows 经典右键菜单 Shell 扩展、E2E 测试、文件关联与图标
- [x] M3/v0.3 —— 密码管理器（DPAPI）、分卷压缩、SFX 自解压、文本/图片不解压预览
- [x] M4-A —— 安装包（Inno Setup）、完整便携包、nightly/正式版本发布流程
- [x] 中英文 GUI、选中解压、完整性测试、加密浏览和后台预览的代码与自动化回归
- [x] 串行批量任务、ZIP/7z 事务编辑、手动版本检查与签名钩子的本地实现
- [ ] Win11 新式菜单生产部署、签名、真实 Win10/Win11 桌面验收及正式发布

## 许可

本项目代码以 MIT 许可发布（见 LICENSE）。

安装包及便携包随附 [7-Zip](https://www.7-zip.org) 26.06 的官方未修改引擎及 SFX 模块，
用户无需自行安装。7-Zip 组件保留 GNU LGPL 2.1-or-later、BSD 通知及 unRAR 限制，
不改用 ggZip 的 MIT 许可。包内 `licenses/7zip/` 包含原始通知与许可全文，
`sources/7zip/7z2606-src.7z` 包含完整对应源码。
详细说明见 `packaging/THIRD_PARTY_NOTICES.md`；引擎二进制及源码由打包脚本获取，
不作为 Git 仓库内的手工维护二进制提交。

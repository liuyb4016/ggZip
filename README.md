# ggZip

Windows 平台压缩软件，目标对标 [Bandizip](https://en.bandisoft.com/bandizip)：压缩包浏览器、智能解压、右键 Shell 集成、加密等。

## 已实现

### v0.2.0 —— Qt GUI 压缩包浏览器（M1）

- **主窗口**：菜单/工具栏/拖拽打开压缩包；条目表格（名称/大小/压缩后/类型，目录排前）；UTF-8 中文路径与文件名正常显示
- **解压到 / 智能解压到**：智能解压为 Bandizip 行为（单根目录直接解压，否则自动建子文件夹）；支持密码
- **新建压缩包**：多选文件与文件夹、输出格式按扩展名（7z/zip/tar/gz/bz2/xz/zst/tar.gz 等）、压缩级别 1-9、密码加密
- **后台执行**：操作在独立线程运行，进度对话框可取消；结果/错误经队列信号回传 GUI 线程
- 基于 Qt6 Widgets（6.9.3 LTS 验证通过），MSVC 与 MinGW 双工具链编译回归通过

### v0.2.1 —— 浏览器增强

- **目录导航**：双击目录进入；"上级"按钮 / Alt+Up 返回；导航栏显示当前路径
- **图片不解压预览**：双击图片条目（png/jpg/jpeg/gif/bmp/webp/ico）提取到临时目录弹窗预览；其他文件双击用系统默认程序打开（临时目录退出时统一清理）
- **列排序**：点击表头按名称/大小/压缩后/类型升降序（目录始终排前）
- **选中统计**：状态栏实时显示已选条目数与合计大小

### v0.1.0 —— 核心引擎 + CLI

- **引擎**：基于 7z.exe 子进程。自动定位顺序：环境变量 `GGZIP_7Z` → 程序同目录（便携分发）→ `PATH` → `C:\Program Files\7-Zip`
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

产物 `build-shell\Release\ggzip_shell.dll`（MSVC，不依赖 Qt/core）。注册到当前用户（无需管理员；Win11 需在右键「显示更多选项」中可见）：

```bat
regsvr32 build-shell\Release\ggzip_shell.dll   &:: 注销加 /u
```

菜单项：智能解压 / 解压到当前文件夹 / 解压到同名文件夹（选中压缩包时），添加到 `<名>.7z` / `.zip`（选中文件或文件夹空白处时）。执行时启动 `ggz.exe`（定位顺序：环境变量 `GGZIP_GGZ` → 注册表 `HKCU\Software\ggZip\GgzPath` → DLL 同目录 → `PATH`）。

自动化测试（不经 Explorer，27 项检查；干跑验证命令行，`--e2e` 走真实 ggz 全链路）：

```bat
build-shell\Release\shell_host_test.exe build-shell\Release\ggzip_shell.dll
set GGZIP_7Z=E:\work\ggZip\tools\7zip\7za.exe
build-shell\Release\shell_host_test.exe build-shell\Release\ggzip_shell.dll build\ggz.exe --e2e
```

## 运行测试

本机需有 7-Zip（或便携 7za.exe）：

```bat
set GGZIP_7Z=C:\Program Files\7-Zip\7z.exe
build\ggzip_smoke.exe
```

## CLI 用法

```bat
ggz list archive.zip
ggz extract archive.7z -o D:\out
ggz smart-extract tool.tar.gz -o D:\tools
ggz create backup.7z D:\docs --level 7 --threads 8
ggz create secret.zip D:\docs -p MyPassword
ggz test archive.zip
ggz where
```

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

## 路线图

- [x] M0/v0.1 —— 核心引擎 + CLI + 智能解压 + 冒烟测试
- [x] M1/v0.2 —— Qt GUI：压缩包浏览器（列表/解压/压缩/智能解压/进度取消）
- [ ] M2/v0.3 —— Windows 右键菜单 Shell 扩展（经典菜单已完成并带 E2E 测试；文件关联图标待做）
- [ ] M3 —— 密码管理器（DPAPI）、分卷压缩、SFX 自解压包
- [ ] M4 —— Win11 新式右键菜单（IExplorerCommand）、安装包（Inno Setup）、多语言

## 许可

本项目代码以 MIT 许可发布（见 LICENSE）。

运行时依赖 [7-Zip](https://www.7-zip.org)（`7z.exe`，LGPL + unRAR 限制）作为外部引擎，
本仓库不分发其二进制；使用 ggZip 时请自行安装 7-Zip 或从官方渠道获取。

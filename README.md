# ggZip

Windows 平台压缩软件，目标对标 [Bandizip](https://en.bandisoft.com/bandizip)：压缩包浏览器、智能解压、右键 Shell 集成、加密等。

## v0.1.0 已实现（核心引擎 + CLI）

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
        进程管理、智能解压决策、格式识别
cli/    ggz 命令行工具
gui/    Qt Widgets GUI（v0.2）
shell/  Windows 右键菜单 COM 扩展（独立 DLL，不依赖 Qt）（v0.3）
```

核心库只依赖 C++20 标准库与 Win32，便于后续被 GUI、Shell 扩展、CLI 复用。

## 路线图

- [x] M0/v0.1 —— 核心引擎 + CLI + 智能解压 + 冒烟测试
- [ ] M1/v0.2 —— Qt GUI：压缩包浏览器（列表/预览）、解压与压缩对话框、设置
- [ ] M2/v0.3 —— Windows 右键菜单 Shell 扩展（经典菜单 + 文件关联图标）
- [ ] M3 —— 密码管理器（DPAPI）、分卷压缩、SFX 自解压包、图片不解压预览
- [ ] M4 —— Win11 新式右键菜单（IExplorerCommand）、安装包（Inno Setup）、多语言

## 许可

本项目代码以 MIT 许可发布（见 LICENSE）。

运行时依赖 [7-Zip](https://www.7-zip.org)（`7z.exe`，LGPL + unRAR 限制）作为外部引擎，
本仓库不分发其二进制；使用 ggZip 时请自行安装 7-Zip 或从官方渠道获取。

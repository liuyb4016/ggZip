# Third-party notices

ggZip is distributed under the MIT license in `LICENSE`.

## Qt

The Windows GUI dynamically links to Qt Core, Gui and Widgets; deployment
also includes Qt Network, Svg, and Qt
platform/image plugins. The packaging workflow uses the open-source Qt 6.9.3
binaries and includes the LGPL v3 and GPL v3 texts in `licenses/`.
The exact Qt version and its source download location are recorded in
`BUILD_INFO.txt`. Qt and its bundled third-party components retain their
respective licenses and copyright notices; consult the matching Qt source
tree's `LICENSES` and `src/3rdparty` directories:

- https://www.qt.io/licensing/open-source-lgpl-obligations
- https://code.qt.io/cgit/qt/qtbase.git/tree/?h=v6.9.3
- https://code.qt.io/cgit/qt/qtsvg.git/tree/?h=v6.9.3

You may replace compatible Qt shared libraries with your own builds.
Do not prohibit reverse engineering for debugging modifications to those libraries.
If you distribute modified Qt binaries, comply with the applicable source and
notice obligations rather than relying on links to the unmodified upstream source.

## Microsoft Visual C++ runtime

The portable package includes x64 redistributable runtime DLLs from the
Visual Studio C++ build tools. These files remain subject to Microsoft's
applicable redistribution terms:

https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files

## 7-Zip

This package includes the official, unmodified 7-Zip 26.06 x64 engine:
`7z/7z.exe`, `7z/7z.dll`, `7z/7z.sfx` and `7z/7zCon.sfx`.
It is a private component of ggZip and does not install the standalone
7-Zip application or change any system 7-Zip installation.

7-Zip Copyright (C) 1999-2026 Igor Pavlov.
These components retain their GNU LGPL 2.1-or-later license and applicable
BSD notices and unRAR license restriction. ggZip's MIT license does not
replace those terms.

The original binary distribution notice is included as `7z/License.txt`
and `licenses/7zip/License.txt`. The full GNU LGPL 2.1 text and original
unRAR license are included in `licenses/7zip/`. The complete corresponding
machine-readable source archive is distributed in the same package as
`sources/7zip/7z2606-src.7z`; its origin and SHA256 are recorded in
`licenses/7zip/SOURCE_INFO.txt`.

The unRAR sources must not be used to develop a RAR-compatible compressor.
ggZip uses this component for RAR extraction, not RAR creation.
You may replace compatible engine binaries with your own builds or select a
custom engine with `GGZIP_7Z`; retain the applicable source and notice obligations.

- https://www.7-zip.org/
- https://github.com/ip7z/7zip/releases/tag/26.06

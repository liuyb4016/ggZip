[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$QtDir,
    [string]$BuildDir = 'build-package',
    [string]$ShellBuildDir = 'build-package-shell',
    [string]$OutputDir = 'out/packages',
    [string]$PackageName = '',
    [string]$IsccPath = '',
    [string]$RuntimeDir = '',
    [string]$SignCertificateThumbprint = '',
    [string]$SignToolPath = '',
    [switch]$SkipBuild,
    [switch]$RunShellTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([bool]$SignCertificateThumbprint -ne [bool]$SignToolPath) { throw 'Provide both signing certificate thumbprint and SignTool path.' }

function Resolve-ProjectPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Invoke-Native([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE"
    }
}

function Find-RuntimeDirectory {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'Set -RuntimeDir to the x64 Microsoft Visual C++ redistributable DLL directory.'
    }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) {
        throw 'Visual C++ build tools were not found; set -RuntimeDir explicitly.'
    }
    $redistRoot = Join-Path $installation 'VC/Redist/MSVC'
    $versions = Get-ChildItem -LiteralPath $redistRoot -Directory |
        Where-Object Name -Match '^\d+\.\d+\.\d+$' |
        Sort-Object { [version]$_.Name } -Descending
    foreach ($version in $versions) {
        $architecture = Join-Path $version.FullName 'x64'
        if (Test-Path -LiteralPath $architecture) {
            $runtime = Get-ChildItem -LiteralPath $architecture -Directory |
                Where-Object Name -Like 'Microsoft.VC*.CRT' | Select-Object -First 1
            if ($runtime) { return $runtime.FullName }
        }
    }
    throw 'The x64 Visual C++ redistributable DLL directory was not found.'
}

$version = (Get-Content -LiteralPath (Join-Path $repoRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION must contain a numeric major.minor.patch version.' }
if (-not $PackageName) { $PackageName = "ggZip-v$version-win64" }
if ($PackageName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') { throw 'Invalid package name.' }
$QtDir = Resolve-ProjectPath $QtDir
$BuildDir = Resolve-ProjectPath $BuildDir
$ShellBuildDir = Resolve-ProjectPath $ShellBuildDir
$OutputDir = Resolve-ProjectPath $OutputDir
$stage = Join-Path $OutputDir $PackageName
$zipPath = Join-Path $OutputDir "$PackageName.zip"
$setupPath = Join-Path $OutputDir "$PackageName-setup.exe"
foreach ($destination in @($stage, $zipPath, $setupPath)) {
    if (Test-Path -LiteralPath $destination) {
        throw "Output already exists: $destination. Use a new -OutputDir or -PackageName."
    }
}
$windeployqt = Join-Path $QtDir 'bin/windeployqt.exe'
if (-not (Test-Path -LiteralPath $windeployqt -PathType Leaf)) { throw "Qt deployment tool not found: $windeployqt" }
if (-not (Test-Path -LiteralPath (Join-Path $QtDir 'plugins/imageformats/qwebp.dll') -PathType Leaf)) {
    throw 'Qt Image Formats is required to deploy WebP. Install the matching qtimageformats development module.'
}
if (-not $IsccPath) {
    $iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($iscc) { $IsccPath = $iscc.Source }
    else { $IsccPath = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe' }
}
$IsccPath = Resolve-ProjectPath $IsccPath
if (-not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) { throw "Inno Setup 6.3+ compiler not found: $IsccPath" }
if (-not $RuntimeDir) { $RuntimeDir = Find-RuntimeDirectory }
$RuntimeDir = Resolve-ProjectPath $RuntimeDir
foreach ($runtimeFile in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $RuntimeDir $runtimeFile) -PathType Leaf)) {
        throw "Required x64 runtime DLL missing: $runtimeFile"
    }
}
if ($RunShellTests -and (Test-Path -LiteralPath 'Registry::HKEY_CURRENT_USER\Software\Classes\CLSID\{28250246-8FC5-415D-95E3-6102C059ECEF}')) {
    throw 'Shell host tests require a disposable environment without an existing ggZip Shell registration.'
}

$previousPath = $env:PATH
$previousEngine = $env:GGZIP_7Z
$engine = & (Join-Path $PSScriptRoot 'prepare-engine.ps1')
$engineManifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot '7zip.json') -Raw | ConvertFrom-Json
try {
    $env:PATH = (Join-Path $QtDir 'bin') + ';' + $env:PATH
    $env:GGZIP_7Z = Join-Path $engine.Directory '7z.exe'
    if (-not $SkipBuild) {
        Invoke-Native cmake @('-S', $repoRoot, '-B', $BuildDir, '-A', 'x64',
            '-DGGZ_BUILD_GUI=ON', '-DGGZ_BUILD_TESTS=ON', "-DCMAKE_PREFIX_PATH=$QtDir")
        Invoke-Native cmake @('--build', $BuildDir, '--config', 'Release', '--parallel')
        Invoke-Native cmake @('-S', (Join-Path $repoRoot 'shell'), '-B', $ShellBuildDir, '-A', 'x64')
        Invoke-Native cmake @('--build', $ShellBuildDir, '--config', 'Release', '--parallel')
    }
    Invoke-Native ctest @('--test-dir', $BuildDir, '-C', 'Release', '--output-on-failure')
    Invoke-Native ctest @('--test-dir', $ShellBuildDir, '-C', 'Release', '--output-on-failure')
    $shellDll = Join-Path $ShellBuildDir 'Release/ggzip_shell.dll'
    $cliExe = Join-Path $BuildDir 'cli/Release/ggz.exe'
    if ($RunShellTests) {
        $hostTest = Join-Path $ShellBuildDir 'Release/shell_host_test.exe'
        Invoke-Native $hostTest @($shellDll, $cliExe)
        Invoke-Native $hostTest @($shellDll, $cliExe, '--e2e')
    }
    New-Item -ItemType Directory -Path $stage | Out-Null
    $validationDir = Join-Path $stage 'packaging'
    New-Item -ItemType Directory -Path $validationDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'VALIDATION.md'), (Join-Path $PSScriptRoot 'PERFORMANCE.md') -Destination $validationDir
    Copy-Item -LiteralPath (Join-Path $BuildDir 'gui/Release/ggzip_gui.exe') -Destination $stage
    Copy-Item -LiteralPath (Join-Path $BuildDir 'gui/Release/ggzip_preview_worker.exe') -Destination $stage
    Copy-Item -LiteralPath $cliExe, $shellDll -Destination $stage
    $bundledEngineDir = Join-Path $stage '7z'
    New-Item -ItemType Directory -Path $bundledEngineDir | Out-Null
    foreach ($file in $engineManifest.payload) {
        Copy-Item -LiteralPath (Join-Path $engine.Directory $file.name) -Destination $bundledEngineDir
    }
    Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md'), (Join-Path $repoRoot 'LICENSE'),
        (Join-Path $PSScriptRoot 'PORTABLE.md'), (Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.md') -Destination $stage
    $guiExe = Join-Path $stage 'ggzip_gui.exe'
    Invoke-Native $windeployqt @('--release', '--no-translations', '--no-compiler-runtime', $guiExe)
    $translationDir = Join-Path $stage 'translations'
    New-Item -ItemType Directory -Path $translationDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $QtDir 'translations/qtbase_zh_CN.qm') -Destination $translationDir
    Get-ChildItem -LiteralPath $RuntimeDir -Filter '*.dll' -File |
        Copy-Item -Destination $stage
    if (-not (Test-Path -LiteralPath (Join-Path $stage 'imageformats/qwebp.dll') -PathType Leaf)) {
        throw 'The deployed image plugins do not include WebP.'
    }
    $codecTest = Join-Path $stage 'ggzip_gui_regression_test.exe'
    Copy-Item -LiteralPath (Join-Path $BuildDir 'gui/Release/ggzip_gui_regression_test.exe') -Destination $codecTest
    try {
        $codecProcess = Start-Process -FilePath $codecTest -ArgumentList '--deployed' -WindowStyle Hidden -PassThru
        if (-not $codecProcess.WaitForExit(120000)) {
            $codecProcess.Kill()
            throw 'Deployed codec regression timed out.'
        }
        if ($codecProcess.ExitCode -ne 0) { throw "Deployed codec regression failed: $($codecProcess.ExitCode)" }
        Write-Host 'Deployed image decoding and GUI-model regressions passed.'
    } finally {
        Remove-Item -LiteralPath $codecTest
    }
    foreach ($binary in @($guiExe, (Join-Path $stage 'ggzip_shell.dll'))) {
        if ((Get-Item -LiteralPath $binary).VersionInfo.FileVersion -ne "$version.0") {
            throw "Binary version does not match VERSION: $binary"
        }
    }
    $cliVersion = & (Join-Path $stage 'ggz.exe') --version
    if ($LASTEXITCODE -ne 0 -or $cliVersion -ne "ggz (ggZip) $version") {
        throw 'CLI version does not match VERSION.'
    }
    $englishHelp = & (Join-Path $stage 'ggz.exe') --lang en --help
    if ($LASTEXITCODE -ne 0 -or $englishHelp -notcontains 'Usage:') {
        throw 'Deployed CLI English help is unavailable.'
    }
    $qtVersion = & (Join-Path $QtDir 'bin/qmake.exe') -query QT_VERSION
    if ($LASTEXITCODE -ne 0 -or $qtVersion -notmatch '^\d+\.\d+\.\d+$') {
        throw 'Unable to determine the Qt version for license notices.'
    }
    $licenses = Join-Path $stage 'licenses'
    New-Item -ItemType Directory -Path $licenses | Out-Null
    foreach ($license in @('LGPL-3.0-only', 'GPL-3.0-only')) {
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/qt/qtbase/v$qtVersion/LICENSES/$license.txt" `
            -OutFile (Join-Path $licenses "Qt-$license.txt")
    }
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/qt/qtimageformats/v$qtVersion/src/3rdparty/libwebp/COPYING" `
        -OutFile (Join-Path $licenses 'libwebp-COPYING.txt')
    $engineLicenses = Join-Path $licenses '7zip'
    $engineSources = Join-Path $stage 'sources/7zip'
    New-Item -ItemType Directory -Path $engineLicenses, $engineSources | Out-Null
    Copy-Item -LiteralPath (Join-Path $engine.Directory 'License.txt') -Destination $engineLicenses
    foreach ($license in $engineManifest.sourceLicenses) {
        Copy-Item -LiteralPath (Join-Path $engine.LicenseDirectory $license.path) `
            -Destination (Join-Path $engineLicenses $license.name)
    }
    Copy-Item -LiteralPath $engine.SourceArchive -Destination $engineSources
    @("7-Zip version: $($engine.Version)", "Source: $($engineManifest.source.url)",
        "Source SHA256: $($engineManifest.source.sha256)",
        "Corresponding source archive: ../../sources/7zip/$($engineManifest.source.name)",
        'The official engine binaries are distributed unmodified.') |
        Set-Content -LiteralPath (Join-Path $engineLicenses 'SOURCE_INFO.txt') -Encoding utf8
    @("ggZip version: $version", "Qt version: $qtVersion",
        "Qt source: https://download.qt.io/archive/qt/$(([version]$qtVersion).Major).$(([version]$qtVersion).Minor)/$qtVersion/",
        "Bundled 7-Zip version: $($engine.Version)",
        "Bundled engine: 7z/7z.exe and 7z/7z.dll; SFX modules included.",
        "7-Zip notices: licenses/7zip; corresponding source: sources/7zip.") |
        Set-Content -LiteralPath (Join-Path $stage 'BUILD_INFO.txt') -Encoding utf8
    & (Join-Path $PSScriptRoot 'test-bundle.ps1') -PackageDir $stage
    if ($SignCertificateThumbprint) {
        & (Join-Path $PSScriptRoot 'sign.ps1') -Files @($guiExe, (Join-Path $stage 'ggz.exe'),
            (Join-Path $stage 'ggzip_preview_worker.exe'), (Join-Path $stage 'ggzip_shell.dll')) `
            -CertificateThumbprint $SignCertificateThumbprint -SignToolPath $SignToolPath
    }
    Compress-Archive -LiteralPath $stage -DestinationPath $zipPath -CompressionLevel Optimal
    Invoke-Native $IsccPath @("/DAppVersion=$version", "/DDistDir=$stage",
        "/DOutputDir=$OutputDir", "/DPackageName=$PackageName", (Join-Path $PSScriptRoot 'ggzip.iss'))
    if (-not (Test-Path -LiteralPath $setupPath -PathType Leaf)) { throw 'Installer output was not produced.' }
    if ($SignCertificateThumbprint) {
        & (Join-Path $PSScriptRoot 'sign.ps1') -Files @($setupPath) `
            -CertificateThumbprint $SignCertificateThumbprint -SignToolPath $SignToolPath
    }
    $checksums = foreach ($file in @($zipPath, $setupPath)) {
        $hash = Get-FileHash -LiteralPath $file -Algorithm SHA256
        "$($hash.Hash.ToLowerInvariant())  $([System.IO.Path]::GetFileName($file))"
    }
    $checksums | Set-Content -LiteralPath (Join-Path $OutputDir "$PackageName-SHA256SUMS.txt") -Encoding utf8
    Write-Host "Portable package: $zipPath"
    Write-Host "Installer: $setupPath"
} finally {
    $env:PATH = $previousPath
    $env:GGZIP_7Z = $previousEngine
}

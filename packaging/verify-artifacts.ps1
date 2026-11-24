[CmdletBinding()]
param(
    [string]$PackageDir = 'out/packages',
    # 期望的包名前缀；为空时从 VERSION 推导 ggZip-v<VERSION>-win64
    [string]$PackageName = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $PackageName) {
    $version = (Get-Content -LiteralPath (Join-Path $repoRoot 'VERSION') -Raw).Trim()
    if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "VERSION 格式无效：$version" }
    $PackageName = "ggZip-v$version-win64"
}
$PackageDir = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $PackageDir))
if (-not (Test-Path -LiteralPath $PackageDir -PathType Container)) {
    throw "产物目录不存在：$PackageDir"
}

$failures = [System.Collections.Generic.List[string]]::new()
function Fail([string]$m) { $failures.Add($m); Write-Host "  [FAIL] $m" }
function Pass([string]$m) { Write-Host "  [ OK ] $m" }

Write-Host "ggZip 分发产物校验（仅检查，不修改任何文件）"
Write-Host "目录: $PackageDir"
Write-Host "包名: $PackageName"
Write-Host ""

# ---- 1. 必需产物存在 ----
Write-Host "[1/4] 必需产物"
$zip = Join-Path $PackageDir "$PackageName.zip"
$setup = Join-Path $PackageDir "$PackageName-setup.exe"
$sums = Join-Path $PackageDir "$PackageName-SHA256SUMS.txt"
foreach ($pair in @(@('便携包', $zip), @('安装器', $setup), @('校验和清单', $sums))) {
    if (Test-Path -LiteralPath $pair[1] -PathType Leaf) { Pass "$($pair[0])：$([System.IO.Path]::GetFileName($pair[1]))" }
    else { Fail "缺少$($pair[0])：$($pair[1])" }
}
if ($failures.Count -gt 0) { Write-Host "`n产物不完整，无法继续校验。"; exit 1 }

# ---- 2. SHA256SUMS 内容验证（等价 CI 的 sha256sum --check）----
Write-Host ""
Write-Host "[2/4] SHA256 校验和"
$sumLines = Get-Content -LiteralPath $sums | Where-Object { $_.Trim() }
if ($sumLines.Count -ne 2) { Fail "校验和清单应恰好 2 行，实际 $($sumLines.Count) 行" }
$covered = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($line in $sumLines) {
    if ($line -notmatch '^([0-9a-fA-F]{64})[\s ]+(.+)$') { Fail "校验和行格式无效：$line"; continue }
    $expected = $Matches[1].ToLowerInvariant()
    $name = $Matches[2].Trim()
    [void]$covered.Add($name)
    $target = Join-Path $PackageDir $name
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { Fail "校验和引用了不存在的文件：$name"; continue }
    $actual = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -eq $expected) { Pass "SHA256 匹配：$name" }
    else { Fail "SHA256 不匹配：$name（期望 $expected，实际 $actual）" }
}
foreach ($required in @([System.IO.Path]::GetFileName($zip), [System.IO.Path]::GetFileName($setup))) {
    if (-not $covered.Contains($required)) { Fail "校验和清单未覆盖必需产物：$required" }
}

# ---- 3. 便携包内容抽查（用固定引擎列出，不解压）----
Write-Host ""
Write-Host "[3/4] 便携包关键文件（只读列出）"
try {
    $engine = & (Join-Path $PSScriptRoot 'prepare-engine.ps1')
    $sevenZip = Join-Path $engine.Directory '7z.exe'
    $listing = & $sevenZip l -sccUTF-8 $zip 2>&1
    if ($LASTEXITCODE -ne 0) { throw "7z 列出失败：$listing" }
    # 7z 输出用反斜杠且带顶层目录前缀，统一为正斜杠便于后缀匹配
    $text = ($listing -join "`n").Replace('\', '/')
    $requiredFiles = @(
        'ggzip_gui.exe', 'ggz.exe', 'ggzip_shell.dll', 'ggzip_preview_worker.exe',
        '7z/7z.exe', '7z/7z.dll', 'imageformats/qwebp.dll',
        'translations/qtbase_zh_CN.qm', 'BUILD_INFO.txt', 'LICENSE',
        'THIRD_PARTY_NOTICES.md', 'sources/7zip/', 'licenses/7zip/'
    )
    foreach ($needle in $requiredFiles) {
        if ($text.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) { Pass "包内含：$needle" }
        else { Fail "包内缺少：$needle" }
    }
} catch {
    Fail "便携包内容检查失败：$($_.Exception.Message)"
}

# ---- 4. 版本一致性 ----
Write-Host ""
Write-Host "[4/4] 版本一致性"
$version = (Get-Content -LiteralPath (Join-Path $repoRoot 'VERSION') -Raw).Trim()
if ($PackageName -eq "ggZip-v$version-win64") { Pass "包名前缀与 VERSION=$version 一致" }
else { Fail "包名 $PackageName 与 VERSION=$version 不一致" }
if ($sumLines -match [regex]::Escape($version)) { Pass "校验和清单文件名含版本号 $version" }
else { Fail "校验和清单内容未体现版本 $version" }

# ---- 结论 ----
Write-Host ""
Write-Host "=========================================="
if ($failures.Count -gt 0) {
    Write-Host "校验未通过：$($failures.Count) 项问题。"
    Write-Host "不要发布该产物；重新运行 packaging/build.ps1 生成新包。"
    exit 1
}
Write-Host "校验通过：产物完整、校验和匹配、关键文件齐全、版本一致。"
Write-Host "注意：此脚本只校验分发产物，不替代 VALIDATION.md 的真实桌面验收与签名核对。"
exit 0

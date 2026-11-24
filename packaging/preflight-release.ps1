[CmdletBinding()]
param(
    # 允许工作区存在未提交改动（仅用于本地演练；正式发布必须保持干净）
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot

$failures = [System.Collections.Generic.List[string]]::new()
$warnings = [System.Collections.Generic.List[string]]::new()

function Fail([string]$message) { $failures.Add($message); Write-Host "  [FAIL] $message" }
function Warn([string]$message) { $warnings.Add($message); Write-Host "  [WARN] $message" }
function Pass([string]$message) { Write-Host "  [ OK ] $message" }

function Invoke-Git([string[]]$Arguments) {
    $output = & git -C $repoRoot @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $output" }
    return $output
}

Write-Host "ggZip 发布前预检（仅检查，不打标签、不推送、不发布）"
Write-Host "仓库: $repoRoot"
Write-Host ""

# ---- 1. 版本号 ----
Write-Host "[1/5] 版本号与标签"
$version = (Get-Content -LiteralPath (Join-Path $repoRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    Fail "VERSION 内容不是 major.minor.patch：'$version'"
} else {
    Pass "VERSION = $version"
}
$tag = "v$version"
$existingTags = @(Invoke-Git @('tag', '--list', $tag))
if ($existingTags -contains $tag) {
    Fail "标签 $tag 已存在；发布工作流会拒绝重复标签，且本地改动无法凭空覆盖远端发布。"
} else {
    Pass "标签 $tag 尚未创建"
}

# ---- 2. 工作区状态 ----
Write-Host ""
Write-Host "[2/5] 工作区状态"
$porcelain = @(Invoke-Git @('status', '--porcelain'))
if ($porcelain.Count -gt 0) {
    $preview = $porcelain | Select-Object -First 15
    Write-Host "  未提交改动（最多显示 15 项，共 $($porcelain.Count) 项）:"
    $preview | ForEach-Object { Write-Host "    $_" }
    if ($AllowDirty) {
        Warn "存在未提交改动，但已通过 -AllowDirty 显式豁免（正式发布不应豁免）。"
    } else {
        Fail "工作区不干净：$($porcelain.Count) 项未提交改动。请先提交或暂存后再打标签。"
    }
} else {
    Pass "工作区干净"
}

# ---- 3. 远程同步状态 ----
Write-Host ""
Write-Host "[3/5] 远程同步状态"
$branch = (Invoke-Git @('rev-parse', '--abbrev-ref', 'HEAD')).Trim()
$upstream = @(Invoke-Git @('rev-parse', '--abbrev-ref', '--symbolic-full-name', '@{u}') 2>$null)
if ($upstream.Count -eq 0 -or [string]::IsNullOrWhiteSpace($upstream[0])) {
    Warn "分支 $branch 未设置上游，无法判断未推送提交。"
} else {
    $ahead = [int]((Invoke-Git @('rev-list', '--count', '@{u}..HEAD')).Trim())
    if ($ahead -gt 0) {
        Fail "本地分支 $branch 领先远端 $ahead 个提交；tag 发布将基于未推送的提交。"
    } else {
        Pass "分支 $branch 与远端同步"
    }
}

# ---- 4. 内置引擎 ----
Write-Host ""
Write-Host "[4/5] 内置 7-Zip 引擎"
try {
    $engine = & (Join-Path $PSScriptRoot 'prepare-engine.ps1')
    $engineExe = Join-Path $engine.Directory '7z.exe'
    if (Test-Path -LiteralPath $engineExe -PathType Leaf) {
        Pass "引擎就绪：$engineExe (7-Zip $($engine.Version))"
    } else {
        Fail "引擎目录存在但缺少 7z.exe：$engineExe"
    }
} catch {
    Fail "内置引擎准备失败：$($_.Exception.Message)"
}

# ---- 5. 发布门槛提醒（人工项，脚本无法代劳）----
Write-Host ""
Write-Host "[5/5] 发布门槛提醒（需人工确认，脚本无法验证）"
foreach ($item in @(
    '当前提交的核心/GUI CTest 与 package.yml（含 FAT32/exFAT、安装/升级/卸载）均通过',
    '真实 Win10 1809+ / Win11 x64 桌面验收完成（安装/关联/右键/预览/加密/分卷/SFX/中英文）',
    '依赖、第三方许可、对应源码、资产与 SHA256 已核对',
    '签名方案已确定并在干净机器验证；未签名包只能作为 prerelease',
    '用户已明确确认提交、推送和正式版本标签'
)) {
    Write-Host "  [ ] $item"
}
Warn "以上人工项未勾选前，即使本预检全部通过也不应发布。"

# ---- 结论 ----
Write-Host ""
Write-Host "=========================================="
if ($failures.Count -gt 0) {
    Write-Host "预检未通过：$($failures.Count) 项阻塞，$($warnings.Count) 项提醒。"
    Write-Host "处理完阻塞项后重跑：.\packaging\preflight-release.ps1"
    exit 1
}
Write-Host "预检通过：无阻塞项（$($warnings.Count) 项提醒）。"
Write-Host "下一步：在真实环境完成上述人工验收，经用户确认后再打标签 v$tag。"
Write-Host "注意：本脚本不会自动提交、推送、打标签或发布。"
exit 0

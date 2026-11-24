[CmdletBinding()]
param(
    [string]$BuildDir = 'build-package',
    [string]$OutputDir = 'out/performance',
    [string]$QtDir = '',
    [ValidateRange(8, 4096)][int]$LargeMiB = 64,
    [ValidateRange(1, 100000)][int]$SmallFiles = 1000,
    [ValidateRange(1, 3)][int]$Iterations = 2,
    [ValidateRange(2048, 8000)][int]$ImageDimension = 4096
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot

function ProjectPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

$BuildDir = ProjectPath $BuildDir
$OutputDir = ProjectPath $OutputDir
$archiveTool = Join-Path $BuildDir 'tests/Release/ggzip_resource_benchmark.exe'
$imageTool = Join-Path $BuildDir 'gui/Release/ggzip_image_resource_benchmark.exe'
$cli = Join-Path $BuildDir 'cli/Release/ggz.exe'
foreach ($required in @($archiveTool, $cli)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Build the benchmark first: $required" }
}
if ($SmallFiles * $Iterations -gt 100000) { throw 'Total small-file output count must not exceed 100000.' }
if (Test-Path -LiteralPath $OutputDir) { throw 'Choose a new output directory; performance reports are never overwritten.' }
$hasImages = Test-Path -LiteralPath $imageTool -PathType Leaf
if ($hasImages) {
    if (-not $QtDir) { throw 'Provide -QtDir when the GUI image benchmark is available.' }
    $QtDir = ProjectPath $QtDir
    if (-not (Test-Path -LiteralPath (Join-Path $QtDir 'bin/Qt6Core.dll') -PathType Leaf)) {
        throw 'The Qt runtime directory is invalid.'
    }
}
New-Item -ItemType Directory -Path $OutputDir | Out-Null
$previousPath = $env:PATH
$previousPlugins = $env:QT_PLUGIN_PATH
$previousEngine = $env:GGZIP_7Z
$skipped = @()
$reports = @()

function Verify-Report([string]$Path) {
    $report = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ($report.schema_version -ne 1 -or $report.passed -ne $true -or @($report.records).Count -eq 0) {
        throw "Benchmark verification failed: $Path"
    }
    foreach ($record in $report.records) {
        if ($record.verified -ne $true -or $record.metrics_available -ne $true) {
            throw "Unverified benchmark result: $($record.name)"
        }
    }
}

try {
    $engine = & (Join-Path $PSScriptRoot 'prepare-engine.ps1')
    $env:GGZIP_7Z = Join-Path $engine.Directory '7z.exe'
    $archiveReport = Join-Path $OutputDir 'archives.json'
    & $archiveTool $cli --large-mib $LargeMiB --small-files $SmallFiles --iterations $Iterations --report $archiveReport
    if ($LASTEXITCODE -eq 77) { $skipped += 'archives: resource guard'; Write-Warning 'Archive benchmark skipped, not counted as passed.' }
    elseif ($LASTEXITCODE -ne 0) { throw "Archive benchmark failed; retain diagnostics in $OutputDir" }
    else { Verify-Report $archiveReport; $reports += $archiveReport }
    if ($hasImages) {
        $env:PATH = (Join-Path $QtDir 'bin') + ';' + $env:PATH
        $env:QT_PLUGIN_PATH = Join-Path $QtDir 'plugins'
        $imageReport = Join-Path $OutputDir 'images.json'
        & $imageTool --dimension $ImageDimension --iterations $Iterations --report $imageReport
        if ($LASTEXITCODE -eq 77) { $skipped += 'images: resource guard'; Write-Warning 'Image benchmark skipped, not counted as passed.' }
        elseif ($LASTEXITCODE -ne 0) { throw "Image benchmark failed; retain diagnostics in $OutputDir" }
        else { Verify-Report $imageReport; $reports += $imageReport }
    } else { $skipped += 'images: GUI target not built' }
    $summary = [ordered]@{
        reports = $reports
        skipped = $skipped
        complete = ($skipped.Count -eq 0)
        note = 'Job counters and before/after logical-size checkpoints are not whole-system memory or physical disk pressure acceptance.'
    }
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDir 'summary.json') -Encoding utf8
    Write-Host "Performance reports: $OutputDir"
} finally {
    $env:PATH = $previousPath
    $env:QT_PLUGIN_PATH = $previousPlugins
    $env:GGZIP_7Z = $previousEngine
}

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Publisher,
    [Parameter(Mandatory = $true)][string]$MakeAppxPath,
    [Parameter(Mandatory = $true)][string]$AssetsDirectory,
    [Parameter(Mandatory = $true)][string]$ApplicationDirectory,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($Publisher -notmatch '^CN=.+$') { throw 'Provide the exact certificate subject as the package publisher.' }
$root = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $root) { throw 'Identity output directory already exists.' }
foreach ($name in @('StoreLogo.png', 'Square150x150Logo.png', 'Square44x44Logo.png')) {
    if (-not (Test-Path -LiteralPath (Join-Path $AssetsDirectory $name) -PathType Leaf)) { throw "Missing identity asset: $name" }
}
$version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot '../../VERSION') -Raw).Trim() + '.0'
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'AppxManifest.xml.in') -Raw
$manifest = $manifest.Replace('@PUBLISHER@', [Security.SecurityElement]::Escape($Publisher)).Replace('@VERSION@', $version)
$content = Join-Path $root 'content'
New-Item -ItemType Directory -Path (Join-Path $content 'Assets') | Out-Null
foreach ($name in @('StoreLogo.png', 'Square150x150Logo.png', 'Square44x44Logo.png')) {
    Copy-Item -LiteralPath (Join-Path $AssetsDirectory $name) -Destination (Join-Path $content 'Assets')
}
$manifest | Set-Content -LiteralPath (Join-Path $content 'AppxManifest.xml') -Encoding utf8
foreach ($binary in @('ggzip_gui.exe', 'ggzip_shell.dll')) {
    Copy-Item -LiteralPath (Join-Path $ApplicationDirectory $binary) -Destination $content
}
$validation = Join-Path $root 'manifest-validation.msix'
& $MakeAppxPath pack /d $content /p $validation
if ($LASTEXITCODE -ne 0) { throw 'Windows manifest/schema validation failed.' }
foreach ($binary in @('ggzip_gui.exe', 'ggzip_shell.dll')) {
    Remove-Item -LiteralPath (Join-Path $content $binary)
}
$package = Join-Path $root 'ggZip-identity.msix'
& $MakeAppxPath pack /d $content /p $package /nv
if ($LASTEXITCODE -ne 0) { throw 'External-location package creation failed.' }
Write-Output $package

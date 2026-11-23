[CmdletBinding()]
param(
    [string]$CacheDir = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot '7zip.json') -Raw | ConvertFrom-Json
if ($manifest.version -notmatch '^\d+\.\d+$') { throw 'Invalid bundled 7-Zip version.' }
if (-not $CacheDir) {
    $CacheDir = Join-Path (Split-Path -Parent $PSScriptRoot) "tools/7zip/$($manifest.version)"
}
$CacheDir = [System.IO.Path]::GetFullPath($CacheDir)
New-Item -ItemType Directory -Path $CacheDir -Force | Out-Null

function Assert-Hash([string]$Path, [string]$ExpectedHash) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $ExpectedHash) {
        throw "7-Zip file missing or checksum mismatch: $Path"
    }
}

function Download-VerifiedFile($Artifact) {
    if ([System.IO.Path]::GetFileName($Artifact.name) -ne $Artifact.name) {
        throw 'Artifact name must not contain directories.'
    }
    $path = Join-Path $CacheDir $Artifact.name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Write-Host "Downloading 7-Zip component: $($Artifact.name)"
        Invoke-WebRequest -Uri $Artifact.url -OutFile $path
    }
    Assert-Hash $path $Artifact.sha256
    return $path
}

$extractor = Download-VerifiedFile $manifest.extractor
$installer = Download-VerifiedFile $manifest.installer
$source = Download-VerifiedFile $manifest.source
$engineDir = Join-Path $CacheDir 'engine'
$licenseDir = Join-Path $CacheDir 'source-licenses'
$needsEngine = $false
foreach ($file in $manifest.payload) {
    $path = Join-Path $engineDir $file.name
    if (Test-Path -LiteralPath $path -PathType Leaf) { Assert-Hash $path $file.sha256 }
    else { $needsEngine = $true }
}
if ($needsEngine) {
    & $extractor x $installer "-o$engineDir" -y | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Unable to extract the official 7-Zip payload.' }
}
foreach ($file in $manifest.payload) {
    Assert-Hash (Join-Path $engineDir $file.name) $file.sha256
}
$needsLicenses = $false
foreach ($license in $manifest.sourceLicenses) {
    $path = Join-Path $licenseDir $license.path
    if (Test-Path -LiteralPath $path -PathType Leaf) { Assert-Hash $path $license.sha256 }
    else { $needsLicenses = $true }
}
if ($needsLicenses) {
    $paths = @($manifest.sourceLicenses | ForEach-Object { $_.path.Replace('/', '\') })
    & $extractor x $source "-o$licenseDir" @paths -y | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Unable to extract the 7-Zip license texts.' }
}
foreach ($license in $manifest.sourceLicenses) {
    Assert-Hash (Join-Path $licenseDir $license.path) $license.sha256
}

[pscustomobject]@{
    Version = $manifest.version
    Directory = $engineDir
    SourceArchive = $source
    LicenseDirectory = $licenseDir
}

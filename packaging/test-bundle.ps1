[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PackageDir = (Resolve-Path -LiteralPath $PackageDir).Path
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot '7zip.json') -Raw | ConvertFrom-Json
$engineDir = Join-Path $PackageDir '7z'
$cli = Join-Path $PackageDir 'ggz.exe'
$gui = Join-Path $PackageDir 'ggzip_gui.exe'
foreach ($file in $manifest.payload) {
    $path = Join-Path $engineDir $file.name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) {
        throw "Bundled engine payload missing or modified: $path"
    }
}
$source = Join-Path $PackageDir "sources/7zip/$($manifest.source.name)"
if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or
    (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $manifest.source.sha256) {
    throw 'The corresponding 7-Zip source archive is missing or modified.'
}
foreach ($license in $manifest.sourceLicenses) {
    $path = Join-Path $PackageDir "licenses/7zip/$($license.name)"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $license.sha256) {
        throw "Required 7-Zip license missing or modified: $path"
    }
}
$licensePath = Join-Path $PackageDir 'licenses/7zip/License.txt'
$expectedLicense = $manifest.payload | Where-Object name -EQ 'License.txt'
if (-not (Test-Path -LiteralPath $licensePath -PathType Leaf) -or
    (Get-FileHash -LiteralPath $licensePath -Algorithm SHA256).Hash -ne $expectedLicense.sha256) {
    throw 'The original 7-Zip distribution notice is missing or modified.'
}

function Invoke-Cli([string[]]$Arguments) {
    & $cli @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Bundled CLI operation failed: $($Arguments[0])" }
}

$environment = @{}
foreach ($name in @('PATH', 'GGZIP_7Z', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM')) {
    $environment[$name] = [System.Environment]::GetEnvironmentVariable($name, 'Process')
}
$tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
$testRoot = Join-Path $tempRoot ("ggzip-bundle-" + [guid]::NewGuid().ToString('N') + ' 中文 空格')
if (Test-Path -LiteralPath $testRoot) { throw 'Bundle test directory already exists.' }
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($name in @('GGZIP_7Z', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM')) {
        [System.Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    $located = & $cli where
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($located) -or
        [System.IO.Path]::GetFullPath($located.Trim()) -ne (Join-Path $engineDir '7z.exe')) {
        throw 'ggZip did not select its bundled engine with GGZIP_7Z unset and a system-only PATH.'
    }
    $process = Start-Process -FilePath $gui -ArgumentList '--selftest' -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Bundled GUI selftest failed: $($process.ExitCode)" }
    $textFile = Join-Path $testRoot '中文文件.txt'
    $binaryFile = Join-Path $testRoot 'binary.bin'
    [System.IO.File]::WriteAllText($textFile, "ggZip 内置引擎验证`n", [System.Text.UTF8Encoding]::new($false))
    $bytes = [byte[]]::new(262144)
    [System.Random]::new(42).NextBytes($bytes)
    [System.IO.File]::WriteAllBytes($binaryFile, $bytes)
    $expectedHash = (Get-FileHash -LiteralPath $binaryFile -Algorithm SHA256).Hash
    foreach ($format in @('zip', '7z', 'exe')) {
        $archive = Join-Path $testRoot "中文压缩包.$format"
        $outDir = Join-Path $testRoot "out-$format"
        $password = @()
        if ($format -eq '7z') { $password = @('-p', 'bundle-test-password') }
        Invoke-Cli (@('create', $archive, $textFile, $binaryFile, '--level', '1') + $password)
        Invoke-Cli (@('test', $archive) + $password)
        Invoke-Cli (@('extract', $archive, '-o', $outDir) + $password)
        if ([System.IO.File]::ReadAllText((Join-Path $outDir '中文文件.txt')) -ne
            [System.IO.File]::ReadAllText($textFile) -or
            (Get-FileHash -LiteralPath (Join-Path $outDir 'binary.bin') -Algorithm SHA256).Hash -ne $expectedHash) {
            throw "Bundled $format roundtrip produced incorrect data."
        }
        Write-Host "Bundled $format create/test/extract roundtrip: OK"
    }
    $volume = Join-Path $testRoot 'split.7z'
    Invoke-Cli @('create', $volume, $binaryFile, '--level', '1', '--volume', '64k')
    if (-not (Test-Path -LiteralPath "$volume.001") -or -not (Test-Path -LiteralPath "$volume.002")) {
        throw 'Bundled engine did not create multiple volumes.'
    }
    $volumeOut = Join-Path $testRoot 'out-volumes'
    Invoke-Cli @('test', $volume)
    Invoke-Cli @('extract', $volume, '-o', $volumeOut)
    if ((Get-FileHash -LiteralPath (Join-Path $volumeOut 'binary.bin') -Algorithm SHA256).Hash -ne $expectedHash) {
        throw 'Bundled volume extraction produced incorrect data.'
    }
    Write-Host 'Bundled engine, Unicode paths, encryption, volumes and SFX verified without a system 7-Zip installation.'
} finally {
    foreach ($name in $environment.Keys) {
        [System.Environment]::SetEnvironmentVariable($name, $environment[$name], 'Process')
    }
    $resolved = [System.IO.Path]::GetFullPath($testRoot)
    if (-not $resolved.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
        ((Get-Item -LiteralPath $resolved).Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw 'Refusing to remove an unexpected bundle test directory.'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

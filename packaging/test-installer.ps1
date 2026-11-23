[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InstallerPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_ACTIONS -ne 'true' -or -not $env:RUNNER_TEMP) {
    throw 'Installer integration tests may only run in a disposable GitHub Actions runner.'
}
$InstallerPath = (Resolve-Path -LiteralPath $InstallerPath).Path
$progid = 'Registry::HKEY_CURRENT_USER\Software\Classes\ggZip.Archive'
$shellKey = 'Registry::HKEY_CURRENT_USER\Software\Classes\CLSID\{28250246-8FC5-415D-95E3-6102C059ECEF}'
foreach ($registration in @($progid, $shellKey)) {
    if (Test-Path -LiteralPath $registration) { throw "Existing registration must not be overwritten: $registration" }
}
$testRoot = [System.IO.Path]::GetFullPath((Join-Path $env:RUNNER_TEMP ("ggzip-install-" + [guid]::NewGuid().ToString('N'))))
$runnerRoot = [System.IO.Path]::GetFullPath($env:RUNNER_TEMP).TrimEnd('\') + '\'
if (-not $testRoot.StartsWith($runnerRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Installer test target is outside RUNNER_TEMP.'
}
New-Item -ItemType Directory -Path $testRoot | Out-Null
$installDir = Join-Path $testRoot 'App with spaces'
$uninstaller = Join-Path $installDir 'unins000.exe'
$uninstalled = $false

function Run-Installer([string]$Executable, [string[]]$Arguments) {
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Installer process failed: $($process.ExitCode)" }
}

try {
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOICONS',
        '/TASKS=associations,shell', "/DIR=`"$installDir`"", "/LOG=`"$(Join-Path $testRoot 'install.log')`"")
    Run-Installer $InstallerPath $arguments
    foreach ($file in @('ggzip_gui.exe', 'ggz.exe', 'ggzip_shell.dll', 'Qt6Core.dll', 'vcruntime140.dll',
        '7z/7z.exe', '7z/7z.dll', '7z/7z.sfx', '7z/7zCon.sfx', 'unins000.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $installDir $file) -PathType Leaf)) {
            throw "Installed file missing: $file"
        }
    }
    & (Join-Path $PSScriptRoot 'test-bundle.ps1') -PackageDir $installDir
    $expectedCommand = '"' + (Join-Path $installDir 'ggzip_gui.exe') + '" "%1"'
    $command = (Get-Item -LiteralPath "$progid\shell\open\command").GetValue('')
    if ($command -ne $expectedCommand) { throw 'Installed association command is incorrect.' }
    if (-not (Test-Path -LiteralPath $shellKey)) { throw 'Shell extension was not registered.' }
    $cliPath = (Get-Item -LiteralPath 'Registry::HKEY_CURRENT_USER\Software\ggZip').GetValue('GgzPath')
    if ($cliPath -ne (Join-Path $installDir 'ggz.exe')) { throw 'Shell launcher path is incorrect.' }
    Run-Installer $InstallerPath $arguments
    & (Join-Path $PSScriptRoot 'test-bundle.ps1') -PackageDir $installDir
    Run-Installer $uninstaller @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
        "/LOG=`"$(Join-Path $testRoot 'uninstall.log')`"")
    $uninstalled = $true
    foreach ($registration in @($progid, $shellKey)) {
        if (Test-Path -LiteralPath $registration) { throw "Registration remains after uninstall: $registration" }
    }
    if ((Get-Item -LiteralPath 'Registry::HKEY_CURRENT_USER\Software\RegisteredApplications').GetValue('ggZip')) {
        throw 'Registered application remains after uninstall.'
    }
    Write-Host 'Installer install, upgrade, and uninstall checks passed.'
} finally {
    if (-not $uninstalled -and (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
        Run-Installer $uninstaller @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
    }
}

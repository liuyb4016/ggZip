[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$ExpectedPublisher)

$ErrorActionPreference = 'Stop'
foreach ($package in @(Get-AppxPackage -Name 'ggZip.Desktop')) {
    if ($package.Publisher -ne $ExpectedPublisher) { throw 'Refusing to remove an identity owned by a different publisher.' }
    Remove-AppxPackage -Package $package.PackageFullName
}

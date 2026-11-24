[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackagePath,
    [Parameter(Mandatory = $true)][string]$ExternalLocation,
    [Parameter(Mandatory = $true)][string]$ExpectedPublisher,
    [switch]$EnableExperimentalModernMenu
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $EnableExperimentalModernMenu) { throw 'Explicit experimental opt-in is required.' }
if ([Environment]::OSVersion.Version.Build -lt 22000) { throw 'Windows 11 is required.' }
$root = (Resolve-Path -LiteralPath $ExternalLocation).Path
$folder = Get-Item -LiteralPath $root
if (-not $folder.PSIsContainer -or ($folder.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'External location must be an ordinary directory.' }
$package = (Resolve-Path -LiteralPath $PackagePath).Path
$locks = @()
try {
    foreach ($file in @($package, (Join-Path $root 'ggzip_gui.exe'), (Join-Path $root 'ggzip_shell.dll'))) {
        $item = Get-Item -LiteralPath $file
        if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Identity binaries must be ordinary files.' }
        $locks += [IO.File]::Open($file, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $signature = Get-AuthenticodeSignature -LiteralPath $file
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -ne $ExpectedPublisher) {
            throw 'Trusted package, GUI and Shell signatures from the expected publisher are required. No certificates are imported.'
        }
    }
    Add-Type -AssemblyName System.IO.Compression
    $archive = [IO.Compression.ZipArchive]::new($locks[0], [IO.Compression.ZipArchiveMode]::Read, $true)
    try {
        $entry = $archive.GetEntry('AppxManifest.xml')
        if (-not $entry -or $entry.Length -gt 1048576) { throw 'Invalid identity manifest.' }
        $stream = $entry.Open()
        try {
            $settings = [Xml.XmlReaderSettings]::new()
            $settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
            $settings.XmlResolver = $null
            $reader = [Xml.XmlReader]::Create($stream, $settings)
            try { $manifest = [Xml.XmlDocument]::new(); $manifest.XmlResolver = $null; $manifest.Load($reader) }
            finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
        $identity = $manifest.SelectSingleNode("/*[local-name()='Package']/*[local-name()='Identity']")
        if (-not $identity -or $identity.LocalName -ne 'Identity' -or $identity.GetAttribute('Name') -ne 'ggZip.Desktop' -or
            $identity.GetAttribute('Publisher') -ne $ExpectedPublisher) { throw 'The signed package has an unexpected identity.' }
    } finally { $archive.Dispose() }
    foreach ($item in @(Get-AppxPackage -Name 'ggZip.Desktop')) {
        if ($item.Publisher -ne $ExpectedPublisher) { throw 'A different publisher owns the existing ggZip identity.' }
    }
    Add-AppxPackage -Path $package -ExternalLocation $root
    Write-Host 'Experimental identity deployed. Classic menu remains available until real Explorer acceptance validates duplicate-menu handling.'
} finally {
    foreach ($handle in $locks) { $handle.Dispose() }
}

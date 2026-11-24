[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string[]]$Files,
    [Parameter(Mandatory = $true)][string]$CertificateThumbprint,
    [Parameter(Mandatory = $true)][string]$SignToolPath,
    [string]$TimestampUrl = 'https://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($CertificateThumbprint -notmatch '^[a-fA-F0-9]{40}$') { throw 'Expected an explicit SHA1 certificate thumbprint.' }
if (-not (Test-Path -LiteralPath $SignToolPath -PathType Leaf)) { throw 'SignTool was not found.' }
$timestamp = [Uri]$TimestampUrl
if ($timestamp.Scheme -ne 'https' -or $timestamp.UserInfo -or -not $timestamp.Host) { throw 'An HTTPS timestamp service is required.' }
$certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint"
if (-not $certificate.HasPrivateKey) { throw 'The current-user certificate has no signing key.' }
if ($certificate.NotAfter -le (Get-Date)) { throw 'Signing certificate is expired.' }
foreach ($file in $Files) {
    $resolved = (Resolve-Path -LiteralPath $file).Path
    $item = Get-Item -LiteralPath $resolved
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Signing targets must be ordinary files.' }
    & $SignToolPath sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $resolved
    if ($LASTEXITCODE -ne 0) { throw "Signing failed: $resolved" }
    & $SignToolPath verify /pa /all $resolved
    if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $resolved" }
    $signature = Get-AuthenticodeSignature -LiteralPath $resolved
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $CertificateThumbprint) {
        throw "Trusted signature does not match the requested signer: $resolved"
    }
}

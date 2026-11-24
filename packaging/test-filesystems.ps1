[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$TestExecutable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_ACTIONS -ne 'true' -or -not $env:RUNNER_TEMP) {
    throw 'Virtual filesystem tests run only on disposable GitHub Actions Windows runners.'
}
$TestExecutable = (Resolve-Path -LiteralPath $TestExecutable).Path
$temporary = (Resolve-Path -LiteralPath $env:RUNNER_TEMP).Path
$fixture = Join-Path $temporary ("ggzip-filesystem-" + [guid]::NewGuid().ToString('N'))
if ([System.IO.Path]::GetFullPath($fixture).StartsWith($temporary.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase) -ne $true) {
    throw 'Filesystem fixture must remain inside the runner temporary directory.'
}
New-Item -ItemType Directory -Path $fixture | Out-Null
$previousVolume = $env:GGZIP_TEST_DISPOSABLE_VOLUME

function Invoke-VirtualDisk([string]$ScriptPath, [string[]]$Commands) {
    Set-Content -LiteralPath $ScriptPath -Value $Commands -Encoding ascii
    $output = & diskpart.exe /s $ScriptPath 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Virtual disk operation failed: $output" }
}

try {
    foreach ($filesystem in @('exFAT', 'FAT32')) {
        $diskFile = Join-Path $fixture "$filesystem.vhd"
        $scriptFile = Join-Path $fixture "$filesystem.diskpart.txt"
        if ($diskFile -match '["\r\n]|[^\x00-\x7F]' -or (Test-Path -LiteralPath $diskFile)) {
            throw 'Virtual disk path must be new, ASCII, and contain no command delimiters.'
        }
        $before = @(Get-Disk | Select-Object -ExpandProperty Number)
        $attached = $false
        try {
            $attached = $true
            Invoke-VirtualDisk $scriptFile @(
                "create vdisk file=`"$diskFile`" maximum=256 type=expandable",
                "select vdisk file=`"$diskFile`"",
                'attach vdisk'
            )
            Update-HostStorageCache
            $image = Get-DiskImage -ImagePath $diskFile
            if (-not $image.Attached) { throw 'The owned test disk image was not attached.' }
            $created = @($image | Get-Disk)
            if ($created.Count -ne 1 -or $created[0].Number -in $before) {
                throw 'Expected exactly one newly attached disk belonging to the owned test image.'
            }
            $disk = $created[0]
            if ([int]$disk.BusType -ne 15 -or $disk.PartitionStyle -ne 'RAW' -or
                $disk.IsBoot -or $disk.IsSystem -or $disk.Size -gt 256MB -or $disk.Size -lt 128MB) {
                throw 'Refusing to initialize a disk that is not the newly attached empty test virtual disk.'
            }
            $initialized = Initialize-Disk -InputObject $disk -PartitionStyle MBR -PassThru -Confirm:$false
            $partition = New-Partition -InputObject $initialized -UseMaximumSize -AssignDriveLetter
            $volume = Format-Volume -Partition $partition -FileSystem $filesystem -NewFileSystemLabel 'GGZIP_TEST' -Confirm:$false -Force
            if (-not $volume.DriveLetter -or $volume.FileSystem -ne $filesystem) {
                throw 'The test virtual volume was not formatted as requested.'
            }
            $env:GGZIP_TEST_DISPOSABLE_VOLUME = "$($volume.DriveLetter):\"
            & $TestExecutable --target-root $env:GGZIP_TEST_DISPOSABLE_VOLUME --disk-pressure
            if ($LASTEXITCODE -ne 0) { throw "$filesystem filesystem tests failed with exit code $LASTEXITCODE." }
        } finally {
            if ($attached) {
                Invoke-VirtualDisk $scriptFile @("select vdisk file=`"$diskFile`"", 'detach vdisk')
                Update-HostStorageCache
            }
        }
        foreach ($owned in @($diskFile, $scriptFile)) {
            $resolved = [System.IO.Path]::GetFullPath($owned)
            if (-not $resolved.StartsWith($fixture + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
                throw 'Refusing to remove a file outside the test fixture.'
            }
            if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Force }
        }
    }
} finally {
    $env:GGZIP_TEST_DISPOSABLE_VOLUME = $previousVolume
    if (Test-Path -LiteralPath $fixture) {
        $remaining = @(Get-ChildItem -LiteralPath $fixture -Force)
        if ($remaining.Count -eq 0) { Remove-Item -LiteralPath $fixture }
        else { Write-Warning "Retained filesystem diagnostics at $fixture" }
    }
}

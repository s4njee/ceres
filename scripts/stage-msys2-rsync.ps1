[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DestinationDir,

    [string]$MsysRoot = $(if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { "C:\msys64" }),

    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Require-File {
    param([string]$Path)
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file not found: $Path"
    }
    (Resolve-Path -LiteralPath $Path).Path
}

function Convert-MsysPath {
    param([string]$Path)
    if ($Path -notmatch '^/usr/bin/(.+)$') {
        throw "Unexpected MSYS2 ldd path: $Path"
    }
    Join-Path $UsrBin $Matches[1]
}

$MsysRoot = (Resolve-Path -LiteralPath $MsysRoot).Path
$UsrBin = Join-Path $MsysRoot "usr\bin"
$DestinationDir = if (Test-Path -LiteralPath $DestinationDir) {
    (Resolve-Path -LiteralPath $DestinationDir).Path
} else {
    New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
    (Resolve-Path -LiteralPath $DestinationDir).Path
}
$StageBin = Join-Path $DestinationDir "rsync\bin"

$rsync = Require-File (Join-Path $UsrBin "rsync.exe")
$ssh = Require-File (Join-Path $UsrBin "ssh.exe")
$bash = Require-File (Join-Path $UsrBin "bash.exe")
$ldd = Require-File (Join-Path $UsrBin "ldd.exe")

if ($Clean -and (Test-Path -LiteralPath $StageBin)) {
    Remove-Item -LiteralPath $StageBin -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $StageBin | Out-Null

$lddOutput = & $bash -lc "ldd /usr/bin/rsync.exe /usr/bin/ssh.exe"
if ($LASTEXITCODE -ne 0) {
    throw "ldd failed for MSYS2 rsync.exe/ssh.exe"
}

$files = [System.Collections.Generic.SortedSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
[void]$files.Add($rsync)
[void]$files.Add($ssh)

foreach ($line in $lddOutput) {
    if ($line -match '=>\s+(/usr/bin/[^ ]+)') {
        [void]$files.Add((Require-File (Convert-MsysPath $Matches[1])))
    }
}

foreach ($file in $files) {
    Copy-Item -LiteralPath $file -Destination $StageBin -Force
}

Write-Host "Staged MSYS2 rsync runtime:"
Write-Host "  Source: $MsysRoot"
Write-Host "  Target: $StageBin"
foreach ($file in $files) {
    Write-Host ("  - " + (Split-Path -Leaf $file))
}

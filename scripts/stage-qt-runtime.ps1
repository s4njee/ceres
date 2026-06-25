[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DestinationDir,

    [string]$QtRoot = $(if ($env:QT_ROOT) { $env:QT_ROOT } else { "C:\msys64\mingw64" }),

    [switch]$CleanQt
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

function Require-Dir {
    param([string]$Path)
    if (!(Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Required directory not found: $Path"
    }
    (Resolve-Path -LiteralPath $Path).Path
}

function Convert-ToMsysPath {
    param([string]$Path)
    $resolved = (Resolve-Path -LiteralPath $Path).Path.Replace('\', '/')
    if ($resolved -notmatch '^([A-Za-z]):/(.*)$') {
        throw "Cannot convert path to MSYS form: $Path"
    }
    '/' + $Matches[1].ToLowerInvariant() + '/' + $Matches[2]
}

function Convert-FromMsysPath {
    param([string]$Path)
    if ($Path -match '^/mingw64/(.+)$') {
        return Join-Path $QtRoot $Matches[1].Replace('/', '\')
    }
    if ($Path -match '^/usr/(.+)$') {
        return Join-Path $MsysRoot ("usr\" + $Matches[1].Replace('/', '\'))
    }
    throw "Unexpected ldd path: $Path"
}

$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$MsysRoot = Split-Path -Parent $QtRoot
$QtBin = Require-Dir (Join-Path $QtRoot "bin")
$QtPlugins = Require-Dir (Join-Path $QtRoot "share\qt6\plugins")
$QtQml = Require-Dir (Join-Path $QtRoot "share\qt6\qml")
$bash = Require-File (Join-Path $MsysRoot "usr\bin\bash.exe")

$DestinationDir = if (Test-Path -LiteralPath $DestinationDir) {
    (Resolve-Path -LiteralPath $DestinationDir).Path
} else {
    New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
    (Resolve-Path -LiteralPath $DestinationDir).Path
}

$ceres = Require-File (Join-Path $DestinationDir "ceres.exe")

if ($CleanQt) {
    foreach ($path in @(
        (Join-Path $DestinationDir "plugins"),
        (Join-Path $DestinationDir "qml")
    )) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }
}

Copy-Item -LiteralPath $QtPlugins -Destination (Join-Path $DestinationDir "plugins") -Recurse -Force
Copy-Item -LiteralPath $QtQml -Destination (Join-Path $DestinationDir "qml") -Recurse -Force

$qtConf = @"
[Paths]
Prefix=.
Plugins=plugins
QmlImports=qml
"@
Set-Content -LiteralPath (Join-Path $DestinationDir "qt.conf") -Value $qtConf -Encoding ASCII

$queue = [System.Collections.Generic.Queue[string]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$copied = [System.Collections.Generic.SortedSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

foreach ($file in @($ceres)) {
    $queue.Enqueue($file)
    [void]$seen.Add($file)
}
Get-ChildItem -LiteralPath (Join-Path $DestinationDir "plugins") -Recurse -Filter *.dll -File |
    ForEach-Object {
        if ($seen.Add($_.FullName)) {
            $queue.Enqueue($_.FullName)
        }
    }
Get-ChildItem -LiteralPath (Join-Path $DestinationDir "qml") -Recurse -Filter *.dll -File |
    ForEach-Object {
        if ($seen.Add($_.FullName)) {
            $queue.Enqueue($_.FullName)
        }
    }

while ($queue.Count -gt 0) {
    $file = $queue.Dequeue()
    $msysPath = Convert-ToMsysPath $file
    $lddOutput = & $bash -lc "PATH=/mingw64/bin:/usr/bin ldd '$msysPath'"
    if ($LASTEXITCODE -ne 0) {
        throw "ldd failed for $file"
    }
    foreach ($line in $lddOutput) {
        if ($line -match '=>\s+(/(?:mingw64|usr)/bin/[^ ]+)') {
            $dep = Require-File (Convert-FromMsysPath $Matches[1])
            $target = Join-Path $DestinationDir (Split-Path -Leaf $dep)
            if (!(Test-Path -LiteralPath $target)) {
                Copy-Item -LiteralPath $dep -Destination $DestinationDir -Force
                [void]$copied.Add((Split-Path -Leaf $dep))
            }
            if ($seen.Add($target)) {
                $queue.Enqueue($target)
            }
        }
    }
}

Write-Host "Staged Qt runtime:"
Write-Host "  Source: $QtRoot"
Write-Host "  Target: $DestinationDir"
Write-Host "  DLLs copied/verified: $($copied.Count)"
Write-Host "  Plugins: plugins"
Write-Host "  QML imports: qml"
Write-Host "  Config: qt.conf"

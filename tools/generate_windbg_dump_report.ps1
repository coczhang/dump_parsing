param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$DumpPath,

    [string]$OutputPath,

    [string]$DebuggerPath,

    [string]$ModuleName = "dump_parsing",

    [string]$CommandSequence = "!analyze -v; .ecxr; kv; lm m dump_parsing; q"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-DebuggerPath {
    param(
        [string]$ExplicitPath
    )

    if ($ExplicitPath) {
        return $ExplicitPath
    }

    $preferredPaths = @(
        "C:\Program Files\Windows Kits\10\Debuggers\x64\cdb.exe",
        "C:\Program Files\Windows Kits\10\Debuggers\x64\windbg.exe",
        "C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe",
        "C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\windbg.exe"
    )

    foreach ($path in $preferredPaths) {
        if (Test-Path -LiteralPath $path) {
            return $path
        }
    }

    foreach ($name in @("cdb.exe", "windbg.exe")) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }
    }

    throw "Neither cdb.exe nor windbg.exe was found. Pass -DebuggerPath explicitly."
}

function Build-DefaultOutputPath {
    param(
        [string]$CrashDumpPath
    )

    $dumpItem = Get-Item -LiteralPath $CrashDumpPath
    return Join-Path $dumpItem.DirectoryName ($dumpItem.BaseName + ".windbg.txt")
}

function Ensure-ParentDirectory {
    param(
        [string]$FilePath
    )

    $directoryPath = Split-Path -Path $FilePath -Parent
    if (-not $directoryPath) {
        return
    }

    if (-not (Test-Path -LiteralPath $directoryPath)) {
        New-Item -ItemType Directory -Path $directoryPath -Force | Out-Null
    }
}

if (-not (Test-Path -LiteralPath $DumpPath)) {
    throw "Dump file was not found: $DumpPath"
}

$resolvedDebuggerPath = Resolve-DebuggerPath -ExplicitPath $DebuggerPath
if (-not (Test-Path -LiteralPath $resolvedDebuggerPath)) {
    throw "Debugger executable does not exist: $resolvedDebuggerPath"
}

if (-not $OutputPath) {
    $OutputPath = Build-DefaultOutputPath -CrashDumpPath $DumpPath
}

Ensure-ParentDirectory -FilePath $OutputPath

$effectiveCommandSequence = $CommandSequence.Replace("dump_parsing", $ModuleName)

if (Test-Path -LiteralPath $OutputPath) {
    Remove-Item -LiteralPath $OutputPath -Force
}

$debuggerOutput = & $resolvedDebuggerPath -z $DumpPath -lines -logo $OutputPath -c $effectiveCommandSequence 2>&1
$exitCode = $LASTEXITCODE

if ($exitCode -ne 0) {
    throw "Debugger exited with code $exitCode. Output: $debuggerOutput"
}

Write-Output "Dump File    : $DumpPath"
Write-Output "Debugger     : $resolvedDebuggerPath"
Write-Output "Commands     : $effectiveCommandSequence"
Write-Output "Report File  : $OutputPath"

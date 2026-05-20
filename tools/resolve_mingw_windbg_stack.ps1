param(
    [string]$InputPath,

    [string]$Text,

    [string]$BuildDir,

    [string]$ExePath,

    [string]$DbgPath,

    [string]$Addr2LinePath,

    [string]$ObjdumpPath,

    [string]$ModuleName = "dump_parsing.exe",

    [switch]$AllowClipboard,

    [switch]$IncludeNonStackMatches
)

. (Join-Path $PSScriptRoot "mingw_crash_tools.ps1")

function Get-StackInputText {
    param(
        [string]$Path,
        [string]$InlineText,
        [bool]$UseClipboard
    )

    if ($InlineText) {
        return $InlineText
    }

    if ($Path) {
        if (-not (Test-Path -LiteralPath $Path)) {
            throw "Input file was not found: $Path"
        }

        return Get-Content -LiteralPath $Path -Raw
    }

    if ($UseClipboard) {
        return Get-Clipboard -Raw
    }

    throw "Pass -InputPath, -Text, or -AllowClipboard."
}

function Escape-RegexLiteral {
    param(
        [string]$Value
    )

    return [Regex]::Escape($Value)
}

function Resolve-ModulePatterns {
    param(
        [string]$Module
    )

    if ($Module.EndsWith(".exe", [System.StringComparison]::OrdinalIgnoreCase)) {
        $baseName = $Module.Substring(0, $Module.Length - 4)
        return @($Module, $baseName)
    }

    return @($Module, "$Module.exe")
}

function Resolve-BuildDirectory {
    param(
        [string]$Path,
        [bool]$HasExplicitBuildDir,
        [string]$ExplicitBuildDir
    )

    if ($HasExplicitBuildDir) {
        return $ExplicitBuildDir
    }

    if ($Path -and (Test-Path -LiteralPath $Path)) {
        $currentDirectory = Split-Path -Path $Path -Parent
        while ($currentDirectory) {
            $cachePath = Join-Path $currentDirectory "CMakeCache.txt"
            $exePath = Join-Path $currentDirectory "dump_parsing.exe"
            if ((Test-Path -LiteralPath $cachePath) -and (Test-Path -LiteralPath $exePath)) {
                return $currentDirectory
            }

            $parentDirectory = Split-Path -Path $currentDirectory -Parent
            if ($parentDirectory -eq $currentDirectory) {
                break
            }
            $currentDirectory = $parentDirectory
        }
    }

    $runtimeDirectory = Resolve-Path (Join-Path $PSScriptRoot "..")
    return $runtimeDirectory.Path
}

function Test-IsStackFrameLine {
    param(
        [string]$LineText
    )

    return $LineText -match '^\s*[0-9A-Fa-f`]+\s+[0-9A-Fa-f`]+\s+:'
}

function Normalize-PathForComparison {
    param(
        [string]$PathText
    )

    if (-not $PathText) {
        return ""
    }

    return (($PathText -replace '\\', '/').TrimEnd('/')).ToLowerInvariant()
}

function Resolve-WorkspaceRoot {
    $workspaceRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
    return Normalize-PathForComparison -PathText $workspaceRoot.Path
}

function Extract-LocationPath {
    param(
        [string]$LocationText
    )

    if (-not $LocationText) {
        return ""
    }

    if ($LocationText -match '^(?<path>[A-Za-z]:[\\/].+?):\d+$') {
        return $Matches["path"]
    }

    return ""
}

function Test-IsWorkspaceLocation {
    param(
        [string]$LocationText,
        [string]$WorkspaceRoot
    )

    $locationPath = Extract-LocationPath -LocationText $LocationText
    if (-not $locationPath) {
        return $false
    }

    $normalizedLocationPath = Normalize-PathForComparison -PathText $locationPath
    return $normalizedLocationPath.StartsWith($WorkspaceRoot)
}

function Test-IsCrashHandlerFunction {
    param(
        [string]$FunctionName
    )

    if (-not $FunctionName) {
        return $false
    }

    foreach ($prefix in @(
            "CrashDumpHandler::writeDump",
            "CrashDumpHandler::handleSignal",
            "CrashDumpHandler::handleTerminate",
            "CrashDumpHandler::unhandledExceptionFilter")) {
        if ($FunctionName.StartsWith($prefix, [System.StringComparison]::Ordinal)) {
            return $true
        }
    }

    return $false
}

function Test-IsUnknownFunction {
    param(
        [string]$FunctionName
    )

    return [string]::IsNullOrWhiteSpace($FunctionName) -or $FunctionName -eq "??"
}

function Test-IsPlaceholderLocation {
    param(
        [string]$LocationText
    )

    return [string]::IsNullOrWhiteSpace($LocationText) -or $LocationText.EndsWith(":?", [System.StringComparison]::Ordinal)
}

function Extract-StackFrames {
    param(
        [string]$SourceText,
        [string]$Module,
        [bool]$AllowNonStackMatches
    )

    $moduleVariants = Resolve-ModulePatterns -Module $Module | ForEach-Object { Escape-RegexLiteral -Value $_ }
    $modulePattern = '(?:' + ($moduleVariants -join '|') + ')'
    $regex = [Regex]::new("(?im)^(?<line>.*?(?<address>\b$modulePattern\+0x[0-9A-Fa-f]+\b).*)$")
    $matches = $regex.Matches($SourceText)

    $frames = New-Object System.Collections.Generic.List[object]
    $seenAddresses = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($match in $matches) {
        $lineText = $match.Groups["line"].Value.Trim()
        if (-not $AllowNonStackMatches -and -not (Test-IsStackFrameLine -LineText $lineText)) {
            continue
        }

        $address = $match.Groups["address"].Value
        if (-not $seenAddresses.Add($address)) {
            continue
        }

        $frames.Add([pscustomobject]@{
            Address = $address
            Line = $lineText
        })
    }

    return $frames
}

function Resolve-FrameDetails {
    param(
        [pscustomobject]$Frame,
        [hashtable]$Context
    )

    $resolution = Resolve-MingwCrashAddress -Context $Context -Address $Frame.Address
    $callStack = @($resolution.ResolvedCallStack)

    return [pscustomobject]@{
        Frame = $Frame
        Resolution = $resolution
        FunctionName = if ($callStack.Count -ge 1) { $callStack[0] } else { "" }
        LocationText = if ($callStack.Count -ge 2) { $callStack[1] } else { "" }
    }
}

function Resolve-LikelyCrashSite {
    param(
        [object[]]$ResolvedFrames,
        [string]$WorkspaceRoot
    )

    foreach ($item in $ResolvedFrames) {
        if (
            (Test-IsWorkspaceLocation -LocationText $item.LocationText -WorkspaceRoot $WorkspaceRoot) `
            -and -not (Test-IsCrashHandlerFunction -FunctionName $item.FunctionName)
        ) {
            return $item
        }
    }

    foreach ($item in $ResolvedFrames) {
        if (
            -not (Test-IsCrashHandlerFunction -FunctionName $item.FunctionName) `
            -and -not (Test-IsUnknownFunction -FunctionName $item.FunctionName) `
            -and -not (Test-IsPlaceholderLocation -LocationText $item.LocationText)
        ) {
            return $item
        }
    }

    foreach ($item in $ResolvedFrames) {
        if (-not (Test-IsCrashHandlerFunction -FunctionName $item.FunctionName)) {
            return $item
        }
    }

    return ($ResolvedFrames | Select-Object -First 1)
}

$inputText = Get-StackInputText -Path $InputPath -InlineText $Text -UseClipboard $AllowClipboard.IsPresent
$frames = @(Extract-StackFrames -SourceText $inputText -Module $ModuleName -AllowNonStackMatches $IncludeNonStackMatches.IsPresent)
$resolvedBuildDir = Resolve-BuildDirectory `
    -Path $InputPath `
    -HasExplicitBuildDir $PSBoundParameters.ContainsKey('BuildDir') `
    -ExplicitBuildDir $BuildDir

if ($frames.Count -eq 0) {
    throw "No '$ModuleName+0x...' or related module-offset entries were found."
}

$context = New-MingwCrashResolverContext `
    -BuildDir $resolvedBuildDir `
    -ExePath $ExePath `
    -DbgPath $DbgPath `
    -Addr2LinePath $Addr2LinePath `
    -ObjdumpPath $ObjdumpPath
$workspaceRoot = Resolve-WorkspaceRoot
$resolvedFrames = @($frames | ForEach-Object { Resolve-FrameDetails -Frame $_ -Context $context })
$likelyCrashSite = Resolve-LikelyCrashSite -ResolvedFrames $resolvedFrames -WorkspaceRoot $workspaceRoot

Write-Output "Module       : $ModuleName"
Write-Output "Frames       : $($frames.Count)"
Write-Output "Mode         : $(if ($IncludeNonStackMatches) { 'Loose' } else { 'StackOnly' })"
Write-Output "BuildDir     : $resolvedBuildDir"
Write-Output "Executable   : $($context.ExePath)"
Write-Output "Symbols      : $($context.SymbolPath)"
Write-Output "ImageBase    : $(Format-UInt64AsHex -Value $context.ImageBase)"
Write-Output ""

if ($likelyCrashSite) {
    Write-Output "Likely Crash Site:"
    Write-Output "Frame        : $($likelyCrashSite.Frame.Line)"
    Write-Output "Address      : $($likelyCrashSite.Resolution.Address)"
    Write-Output "Function     : $($likelyCrashSite.FunctionName)"
    Write-Output "Location     : $($likelyCrashSite.LocationText)"
    Write-Output ""
}

foreach ($item in $resolvedFrames) {
    $resolution = $item.Resolution
    Write-Output "Frame        : $($item.Frame.Line)"
    Write-Output "Address      : $($resolution.Address)"

    if ($item.FunctionName) {
        Write-Output "Function     : $($item.FunctionName)"
    }

    if ($item.LocationText) {
        Write-Output "Location     : $($item.LocationText)"
    }

    if ($resolution.ResolvedCallStack.Count -gt 2) {
        Write-Output "Inline       :"
        for ($i = 2; $i -lt $resolution.ResolvedCallStack.Count; $i += 2) {
            $functionName = $resolution.ResolvedCallStack[$i]
            $locationText = if ($i + 1 -lt $resolution.ResolvedCallStack.Count) { $resolution.ResolvedCallStack[$i + 1] } else { "" }
            Write-Output "  $functionName"
            if ($locationText) {
                Write-Output "  $locationText"
            }
        }
    }

    Write-Output ""
}

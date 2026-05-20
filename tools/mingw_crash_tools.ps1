Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-DefaultBuildDirectory {
    $runtimeDirectory = Resolve-Path (Join-Path $PSScriptRoot "..")
    return $runtimeDirectory.Path
}

function Resolve-DefaultExePath {
    param(
        [string]$BaseDirectory
    )

    return Join-Path $BaseDirectory "dump_parsing.exe"
}

function Resolve-DefaultDbgPath {
    param(
        [string]$ExecutablePath
    )

    return "$ExecutablePath.dbg"
}

function Resolve-Addr2LinePath {
    param(
        [string]$BuildDirectory,
        [string]$ExplicitPath
    )

    if ($ExplicitPath) {
        return $ExplicitPath
    }

    $cachePath = Join-Path $BuildDirectory "CMakeCache.txt"
    if (Test-Path -LiteralPath $cachePath) {
        $cacheLine = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_ADDR2LINE:FILEPATH=(.+)$' | Select-Object -First 1
        if ($cacheLine) {
            return $cacheLine.Matches[0].Groups[1].Value
        }
    }

    $command = Get-Command addr2line.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "addr2line.exe was not found. Pass -Addr2LinePath explicitly."
}

function Resolve-ObjdumpPath {
    param(
        [string]$BuildDirectory,
        [string]$ExplicitPath
    )

    if ($ExplicitPath) {
        return $ExplicitPath
    }

    $cachePath = Join-Path $BuildDirectory "CMakeCache.txt"
    if (Test-Path -LiteralPath $cachePath) {
        $cacheLine = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_OBJDUMP:FILEPATH=(.+)$' | Select-Object -First 1
        if ($cacheLine) {
            return $cacheLine.Matches[0].Groups[1].Value
        }
    }

    $command = Get-Command objdump.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "objdump.exe was not found. Pass -ObjdumpPath explicitly."
}

function Normalize-AddressInput {
    param(
        [string]$InputAddress
    )

    $trimmed = $InputAddress.Trim()

    if ($trimmed -match '^[^+]+\+(0x[0-9A-Fa-f]+)$') {
        return @{
            Value = $Matches[1]
            IsModuleOffset = $true
        }
    }

    if ($trimmed -match '^(0x)?[0-9A-Fa-f]+$') {
        $normalized = $trimmed
        if (-not $trimmed.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
            $normalized = "0x$trimmed"
        }

        return @{
            Value = $normalized
            IsModuleOffset = $false
        }
    }

    throw "Unsupported address format: $InputAddress"
}

function Convert-HexToUInt64 {
    param(
        [string]$HexValue
    )

    return [Convert]::ToUInt64($HexValue.Substring(2), 16)
}

function Format-UInt64AsHex {
    param(
        [UInt64]$Value
    )

    return ('0x{0:x}' -f $Value)
}

function Resolve-ImageBase {
    param(
        [string]$ExecutablePath,
        [string]$ObjdumpToolPath
    )

    $output = & $ObjdumpToolPath -p $ExecutablePath 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "objdump failed while reading image base: $output"
    }

    $line = $output | Select-String 'ImageBase\s+([0-9A-Fa-f]+)' | Select-Object -First 1
    if (-not $line) {
        throw "ImageBase was not found in objdump output."
    }

    return [Convert]::ToUInt64($line.Matches[0].Groups[1].Value, 16)
}

function Invoke-Addr2Line {
    param(
        [string]$ToolPath,
        [string]$SymbolPath,
        [string]$TargetAddress
    )

    if (-not (Test-Path -LiteralPath $SymbolPath)) {
        throw "Symbol file not found: $SymbolPath"
    }

    $output = & $ToolPath -C -f -i -e $SymbolPath $TargetAddress 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "addr2line failed for '$SymbolPath': $output"
    }

    return @($output)
}

function Looks-Unresolved {
    param(
        [string[]]$Addr2LineOutput
    )

    return ($Addr2LineOutput.Count -ge 2 -and $Addr2LineOutput[0] -eq '??' -and $Addr2LineOutput[1] -eq '??:0')
}

function New-MingwCrashResolverContext {
    param(
        [string]$BuildDir,
        [string]$ExePath,
        [string]$DbgPath,
        [string]$Addr2LinePath,
        [string]$ObjdumpPath
    )

    if (-not $BuildDir) {
        $BuildDir = Resolve-DefaultBuildDirectory
    }

    if (-not $ExePath) {
        $ExePath = Resolve-DefaultExePath -BaseDirectory $BuildDir
    }

    if (-not $DbgPath) {
        $DbgPath = Resolve-DefaultDbgPath -ExecutablePath $ExePath
    }

    $resolvedAddr2LinePath = Resolve-Addr2LinePath -BuildDirectory $BuildDir -ExplicitPath $Addr2LinePath
    $resolvedObjdumpPath = Resolve-ObjdumpPath -BuildDirectory $BuildDir -ExplicitPath $ObjdumpPath

    if (-not (Test-Path -LiteralPath $resolvedAddr2LinePath)) {
        throw "addr2line.exe does not exist: $resolvedAddr2LinePath"
    }

    if (-not (Test-Path -LiteralPath $resolvedObjdumpPath)) {
        throw "objdump.exe does not exist: $resolvedObjdumpPath"
    }

    if (-not (Test-Path -LiteralPath $ExePath)) {
        throw "Executable was not found: $ExePath"
    }

    $symbolPath = $DbgPath
    if (-not (Test-Path -LiteralPath $symbolPath)) {
        Write-Warning "DBG file was not found. Falling back to the executable image."
        $symbolPath = $ExePath
    }

    return @{
        BuildDir = $BuildDir
        ExePath = $ExePath
        SymbolPath = $symbolPath
        Addr2LinePath = $resolvedAddr2LinePath
        ObjdumpPath = $resolvedObjdumpPath
        ImageBase = Resolve-ImageBase -ExecutablePath $ExePath -ObjdumpToolPath $resolvedObjdumpPath
    }
}

function Resolve-MingwCrashAddress {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$Context,

        [Parameter(Mandatory = $true)]
        [string]$Address
    )

    $normalizedAddressInfo = Normalize-AddressInput -InputAddress $Address
    $targetAddress = $normalizedAddressInfo.Value

    if ($normalizedAddressInfo.IsModuleOffset) {
        $targetAddress = Format-UInt64AsHex -Value ($Context.ImageBase + (Convert-HexToUInt64 -HexValue $normalizedAddressInfo.Value))
    } else {
        $rawAddressValue = Convert-HexToUInt64 -HexValue $normalizedAddressInfo.Value
        if ($rawAddressValue -lt $Context.ImageBase) {
            $targetAddress = Format-UInt64AsHex -Value ($Context.ImageBase + $rawAddressValue)
        }
    }

    $result = Invoke-Addr2Line -ToolPath $Context.Addr2LinePath -SymbolPath $Context.SymbolPath -TargetAddress $targetAddress

    if (Looks-Unresolved -Addr2LineOutput $result -and -not $normalizedAddressInfo.IsModuleOffset) {
        $rawAddressValue = Convert-HexToUInt64 -HexValue $normalizedAddressInfo.Value
        $fallbackAddress = Format-UInt64AsHex -Value ($Context.ImageBase + $rawAddressValue)
        if ($fallbackAddress -ne $targetAddress) {
            $result = Invoke-Addr2Line -ToolPath $Context.Addr2LinePath -SymbolPath $Context.SymbolPath -TargetAddress $fallbackAddress
            $targetAddress = $fallbackAddress
        }
    }

    return @{
        Input = $Address
        Address = $targetAddress
        ImageBase = Format-UInt64AsHex -Value $Context.ImageBase
        Executable = $Context.ExePath
        Symbols = $Context.SymbolPath
        Addr2Line = $Context.Addr2LinePath
        Objdump = $Context.ObjdumpPath
        ResolvedCallStack = @($result)
    }
}

function Write-MingwCrashResolution {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$Resolution
    )

    Write-Output "Input        : $($Resolution.Input)"
    Write-Output "Address      : $($Resolution.Address)"
    Write-Output "ImageBase    : $($Resolution.ImageBase)"
    Write-Output "Executable   : $($Resolution.Executable)"
    Write-Output "Symbols      : $($Resolution.Symbols)"
    Write-Output "addr2line    : $($Resolution.Addr2Line)"
    Write-Output "objdump      : $($Resolution.Objdump)"
    Write-Output ""
    Write-Output "Resolved Call Stack:"
    $Resolution.ResolvedCallStack | ForEach-Object { Write-Output $_ }
}

param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Address,

    [string]$BuildDir,

    [string]$ExePath,

    [string]$DbgPath,

    [string]$Addr2LinePath,

    [string]$ObjdumpPath
)

. (Join-Path $PSScriptRoot "mingw_crash_tools.ps1")

$context = New-MingwCrashResolverContext `
    -BuildDir $BuildDir `
    -ExePath $ExePath `
    -DbgPath $DbgPath `
    -Addr2LinePath $Addr2LinePath `
    -ObjdumpPath $ObjdumpPath

$resolution = Resolve-MingwCrashAddress -Context $context -Address $Address
Write-MingwCrashResolution -Resolution $resolution

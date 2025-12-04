param(
    [string]$BuildDir = "build",
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Arch = "x64",
    [string[]]$Targets = @(),
    [switch]$RunTests
)

$builder = Join-Path $PSScriptRoot "build.ps1"

$common = @{
    BuildDir = $BuildDir
    Generator = $Generator
    Arch = $Arch
}

if ($Targets.Count -gt 0) {
    $common["Targets"] = $Targets
}

$debugArgs = $common.Clone()
$debugArgs["Config"] = "Debug"
if ($RunTests) {
    $debugArgs["RunTests"] = $true
}

& $builder @debugArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$releaseArgs = $common.Clone()
$releaseArgs["Config"] = "Release"
if ($RunTests) {
    $releaseArgs["RunTests"] = $true
}

& $builder @releaseArgs
exit $LASTEXITCODE

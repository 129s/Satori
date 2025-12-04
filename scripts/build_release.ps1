param(
    [string]$BuildDir = "build",
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Arch = "x64",
    [string[]]$Targets = @(),
    [switch]$RunTests
)

$builder = Join-Path $PSScriptRoot "build.ps1"

$arguments = @{
    Config = "Release"
    BuildDir = $BuildDir
    Generator = $Generator
    Arch = $Arch
}

if ($Targets.Count -gt 0) {
    $arguments["Targets"] = $Targets
}
if ($RunTests) {
    $arguments["RunTests"] = $true
}

& $builder @arguments
exit $LASTEXITCODE

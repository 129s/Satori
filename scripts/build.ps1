param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [string]$BuildDir = "build",
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Arch = "x64",
    [string[]]$Targets = @(),
    [switch]$RunTests
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir
} else {
    Join-Path $repoRoot $BuildDir
}

if (-not (Test-Path $buildPath)) {
    New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
}

function Invoke-ExternalCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    Write-Host "==> $Command $($Arguments -join ' ')" -ForegroundColor Cyan
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        Write-Error "命令 $Command 失败 (exit code $LASTEXITCODE)"
        exit $LASTEXITCODE
    }
}

$configureArgs = @(
    "-S", $repoRoot,
    "-B", $buildPath,
    "-G", $Generator,
    "-A", $Arch
)
Invoke-ExternalCommand -Command "cmake" -Arguments $configureArgs

$buildArgs = @(
    "--build", $buildPath,
    "--config", $Config
)

if ($Targets.Count -gt 0) {
    foreach ($target in $Targets) {
        Invoke-ExternalCommand -Command "cmake" -Arguments ($buildArgs + @("--target", $target))
    }
} else {
    Invoke-ExternalCommand -Command "cmake" -Arguments $buildArgs
}

if ($RunTests) {
    $ctestArgs = @(
        "--test-dir", $buildPath,
        "-C", $Config,
        "--output-on-failure"
    )
    Invoke-ExternalCommand -Command "ctest" -Arguments $ctestArgs
}

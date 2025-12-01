param(
    [string]$BuildDir = "build",
    [string]$StagingDir = ""
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $root
if ([string]::IsNullOrEmpty($StagingDir)) {
    $StagingDir = Join-Path $BuildDir "package"
}

$buildRoot = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir
} else {
    Join-Path $repoRoot $BuildDir
}

$binDir = Join-Path $buildRoot "Release"
if (-not (Test-Path $binDir)) {
    $binDir = $buildRoot
}

$exePath = Join-Path $binDir "SatoriWin.exe"
if (-not (Test-Path $exePath)) {
    Write-Error "未找到 SatoriWin.exe，请先构建 Release 配置。"
    exit 1
}

$absoluteStaging = if ([System.IO.Path]::IsPathRooted($StagingDir)) {
    $StagingDir
} else {
    Join-Path $repoRoot $StagingDir
}

New-Item -ItemType Directory -Force -Path $absoluteStaging | Out-Null
Copy-Item -Force $exePath $absoluteStaging
Copy-Item -Recurse -Force (Join-Path $repoRoot "presets") (Join-Path $absoluteStaging "presets")
Copy-Item -Force (Join-Path $repoRoot "README.md") $absoluteStaging
Copy-Item -Force (Join-Path $repoRoot "docs/troubleshooting/win_audio.md") $absoluteStaging

Write-Host "已将 SatoriWin 打包到 $absoluteStaging"

# Builds Poker Smash (game + launcher) from source.
#
# Prerequisites:
#   - Visual Studio 2022+ (with the C++ workload)
#   - CMake 3.25+, Ninja
#   - ReXGlue SDK built and installed (see README)
#
# The SDK must be built with the backends you want to ship. Direct3D 12 is on
# by default; add -DREXGLUE_USE_VULKAN=ON when configuring the SDK to also get
# the Vulkan backend inside rexgpu-xenos.dll.
#
# Run this inside the VS developer shell so MSVC and the Windows SDK are on
# PATH:
#   & 'D:\Visual Studio\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64
#
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1

$ErrorActionPreference = 'Stop'

# Put MSVC and the Windows SDK on PATH.
& 'D:\Visual Studio\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

$project = Join-Path $PSScriptRoot 'pokersmash'
Set-Location $project

Write-Host '=== codegen ==='
rexglue codegen pokersmash_manifest.toml
if ($LASTEXITCODE -ne 0) { Write-Warning "codegen returned $LASTEXITCODE (the CLI exits 1 on success-like runs; check the output)" }

Write-Host '=== configure ==='
cmake --preset win-amd64-release

Write-Host '=== build ==='
cmake --build out/build/win-amd64-release

$outDir = 'out/build/win-amd64-release'
$exe = Join-Path $outDir 'pokersmash.exe'
$launcher = Join-Path $outDir 'pokersmash_launcher.exe'

if ((Test-Path $exe) -and (Test-Path $launcher)) {
    Write-Host "`nBuilt:"
    Write-Host "  $((Resolve-Path $exe).Path)"
    Write-Host "  $((Resolve-Path $launcher).Path)"
} else {
    Write-Error 'Build did not produce both pokersmash.exe and pokersmash_launcher.exe'
}

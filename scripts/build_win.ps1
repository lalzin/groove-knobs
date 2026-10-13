# Release build on Windows (Visual Studio 2022): VST3 + Standalone.
#   powershell -ExecutionPolicy Bypass -File scripts\build_win.ps1
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "build-win"
cmake -S $Root -B $Build -G "Visual Studio 17 2022" -A x64
cmake --build $Build --config Release --parallel
ctest --test-dir $Build -C Release --output-on-failure
Write-Host "Built. Install with scripts\make_installer_win.ps1 (or configure with -DGK_COPY_PLUGINS=ON from an elevated shell)."

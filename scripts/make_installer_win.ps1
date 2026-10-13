# Builds dist\FrenchFilter-<version>-Windows-Setup.exe (VST3 + standalone, x64).
#   powershell -ExecutionPolicy Bypass -File scripts\make_installer_win.ps1 [-SkipBuild]
# Requires Inno Setup 6 (iscc.exe; winget install JRSoftware.InnoSetup).
#
# Free public download: without an Authenticode certificate, Windows
# SmartScreen shows "Windows protected your PC" (More info > Run anyway).
# To sign, set WIN_SIGN_CERT (path to .pfx) and WIN_SIGN_PASSWORD.
param([switch]$SkipBuild)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Ids = Get-Content (Join-Path $Root "cmake\GrooveIds.cmake") -Raw
$Version = [regex]::Match($Ids, 'set\(FF_VERSION\s+"([^"]+)"').Groups[1].Value
$Build = Join-Path $Root "build-win"

if (-not $SkipBuild) {
  cmake -S $Root -B $Build -G "Visual Studio 17 2022" -A x64 -DGK_COPY_PLUGINS=OFF
  cmake --build $Build --config Release --parallel
  ctest --test-dir $Build -C Release --output-on-failure
}
$Art = Join-Path $Build "plugins\french_filter\FrenchFilter_artefacts\Release"

function Sign($file) {
  if ($env:WIN_SIGN_CERT) {
    signtool sign /f $env:WIN_SIGN_CERT /p $env:WIN_SIGN_PASSWORD /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $file
  }
}
Sign (Join-Path $Art "Standalone\French Filter.exe")
Get-ChildItem (Join-Path $Art "VST3\French Filter.vst3") -Recurse -Filter *.vst3 -File | ForEach-Object { Sign $_.FullName }

$Iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $Iscc) { $Iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
& $Iscc "/DMyAppVersion=$Version" "/DBuildDir=$Art" (Join-Path $Root "installer\windows\FrenchFilter.iss")

$Setup = Join-Path $Root "dist\FrenchFilter-$Version-Windows-Setup.exe"
Sign $Setup
Write-Host "Installer: $Setup"

# Builds the Windows installers (VST3 + standalone, x64) of the Groove Knobs plugins:
#   dist\<Plugin>-<version>-Windows-Setup.exe
#   powershell -ExecutionPolicy Bypass -File scripts\make_installer_win.ps1 [-Plugin FF|BT|BW|PP|SIB|KL|Suite|All] [-SkipBuild]
# Requires Visual Studio 2022 and Inno Setup 6 (winget install JRSoftware.InnoSetup).
#
# Free public download: without an Authenticode certificate, Windows
# SmartScreen shows "Windows protected your PC" (More info > Run anyway).
# To sign, set WIN_SIGN_CERT (path to .pfx) and WIN_SIGN_PASSWORD.
param([string]$Plugin = "All", [switch]$SkipBuild)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Ids = Get-Content (Join-Path $Root "cmake\GrooveIds.cmake") -Raw
$Build = Join-Path $Root "build-win"

# On GitHub Actions, a failure is also shown as an annotation (readable
# without signing in, unlike the log): its first error lines
function Report($Lines, $Pattern, $What) {
  $hits = $Lines | ForEach-Object { "$_" } | Select-String -Pattern $Pattern -Context 0,2 | Select-Object -First 25 |
    ForEach-Object { (@($_.Line) + $_.Context.PostContext) -join " | " }
  if ($env:GITHUB_ACTIONS) { Write-Host ("::error title=$What::" + (($hits -join "%0A") -replace "::", ": :")) }
  throw "$What failed"
}

if (-not $SkipBuild) {
  cmake -S $Root -B $Build -G "Visual Studio 17 2022" -A x64 -DGK_COPY_PLUGINS=OFF
  if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
  cmake --build $Build --config Release --parallel 2>&1 | Tee-Object -Variable BuildOut
  if ($LASTEXITCODE -ne 0) { Report $BuildOut "error|fatal" "Build" }
  ctest --test-dir $Build -C Release --output-on-failure 2>&1 | Tee-Object -Variable TestOut
  if ($LASTEXITCODE -ne 0) { Report $TestOut "FAILED|Failed|failed|with expansion|error|Exception" "Tests" }
}

function Sign($file) {
  if ($env:WIN_SIGN_CERT) {
    signtool sign /f $env:WIN_SIGN_CERT /p $env:WIN_SIGN_PASSWORD /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $file
  }
}

$Iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $Iscc) { $Iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }

$All = @{
  FF = @{ Name = "French Filter"; File = "FrenchFilter"; Dir = "french_filter\FrenchFilter_artefacts" };
  BT = @{ Name = "Boogie Trio";   File = "BoogieTrio";   Dir = "boogie_trio\BoogieTrio_artefacts" };
  BW = @{ Name = "Backwash";      File = "Backwash";     Dir = "backwash\Backwash_artefacts" };
  PP = @{ Name = "Pew Pew";       File = "PewPew";       Dir = "pew_pew\PewPew_artefacts" };
  SIB = @{ Name = "Sing It Back"; File = "SingItBack";   Dir = "sing_it_back\SingItBack_artefacts" };
  KL = @{ Name = "Kick Lock";     File = "KickLock";     Dir = "kick_lock\KickLock_artefacts" }
}
$Targets = if ($Plugin -eq "All") { @("FF", "BT", "BW", "PP", "SIB", "KL") } elseif ($Plugin -eq "Suite") { @() } else { @($Plugin) }

foreach ($P in $Targets) {
  $Info = $All[$P]
  $Version = [regex]::Match($Ids, "set\(${P}_VERSION\s+`"([^`"]+)`"").Groups[1].Value
  $Art = Join-Path $Build ("plugins\" + $Info.Dir + "\Release")
  Sign (Join-Path $Art ("Standalone\" + $Info.Name + ".exe"))
  Get-ChildItem (Join-Path $Art ("VST3\" + $Info.Name + ".vst3")) -Recurse -Filter *.vst3 -File | ForEach-Object { Sign $_.FullName }
  $SendArt = Join-Path $Build "plugins\kick_lock_send\KickLockSend_artefacts\Release"
  if ($P -eq "KL") { Get-ChildItem (Join-Path $SendArt "VST3\Kick Lock Send.vst3") -Recurse -Filter *.vst3 -File | ForEach-Object { Sign $_.FullName } }
  & $Iscc "/DPlugin=$P" "/DMyAppVersion=$Version" "/DBuildDir=$Art" "/DSendDir=$SendArt" (Join-Path $Root "installer\windows\GrooveKnobs.iss")
  if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed for $P" }
  $Setup = Join-Path $Root ("dist\" + $Info.File + "-$Version-Windows-Setup.exe")
  Sign $Setup
  Write-Host "Installer: $Setup"
}

# The whole suite in one installer (with -Plugin All or -Plugin Suite)
if ($Plugin -eq "All" -or $Plugin -eq "Suite") {
  $SuiteVersion = [regex]::Match($Ids, "set\(GK_SUITE_VERSION\s+`"([^`"]+)`"").Groups[1].Value
  foreach ($P in $All.Keys) {
    $V = [regex]::Match($Ids, "set\(${P}_VERSION\s+`"([^`"]+)`"").Groups[1].Value
    if ($V -ne $SuiteVersion) { throw "$P is $V but the suite is $SuiteVersion (cmake/GrooveIds.cmake)" }
  }
  & $Iscc "/DMyAppVersion=$SuiteVersion" "/DBuildRoot=$(Join-Path $Build 'plugins')" (Join-Path $Root "installer\windows\GrooveKnobsSuite.iss")
  if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed for the suite" }
  $Setup = Join-Path $Root ("dist\GrooveKnobs-$SuiteVersion-Windows-Setup.exe")
  Sign $Setup
  Write-Host "Installer: $Setup"
}

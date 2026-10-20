; Groove Knobs — Windows installer for one plugin (Inno Setup 6).
; Built by scripts/make_installer_win.ps1 -Plugin FF|BT|BW|PP|SIB, or:
;   iscc /DPlugin=BT /DMyAppVersion=0.1.0 /DBuildDir=<...>\BoogieTrio_artefacts\Release installer\windows\GrooveKnobs.iss

#ifndef Plugin
  #define Plugin "FF"
#endif
#if Plugin == "PP"
  #define MyAppName "Pew Pew"
  #define MyFileName "PewPew"
  #define MyAppId "{{A7969E38-1C2F-4467-9340-8E07EA24D4C6}"
  #define MyBuildFolder "pew_pew\PewPew_artefacts"
#elif Plugin == "SIB"
  #define MyAppName "Sing It Back"
  #define MyFileName "SingItBack"
  #define MyAppId "{{6A02801C-62DA-4598-A258-1987E4C95FB3}"
  #define MyBuildFolder "sing_it_back\SingItBack_artefacts"
#elif Plugin == "BT"
  #define MyAppName "Boogie Trio"
  #define MyFileName "BoogieTrio"
  #define MyAppId "{{37F3C7E7-C8E0-4CD8-A030-EB1F2A78579B}"
  #define MyBuildFolder "boogie_trio\BoogieTrio_artefacts"
#elif Plugin == "BW"
  #define MyAppName "Backwash"
  #define MyFileName "Backwash"
  #define MyAppId "{{91974E06-8EC9-4C52-9F5E-440C0D25628B}"
  #define MyBuildFolder "backwash\Backwash_artefacts"
#else
  #define MyAppName "French Filter"
  #define MyFileName "FrenchFilter"
  #define MyAppId "{{052BE9F5-4CAF-4309-A07D-2ED20F62E6A6}"
  #define MyBuildFolder "french_filter\FrenchFilter_artefacts"
#endif
#define MyPublisher "LALZIN"
#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build-win\plugins\" + MyBuildFolder + "\Release"
#endif

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyPublisher}
DefaultDirName={autopf}\{#MyPublisher}\{#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
LicenseFile=..\common\LICENSE.txt
OutputDir=..\..\dist
OutputBaseFilename={#MyFileName}-{#MyAppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName} ({#MyPublisher})
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyPublisher}
VersionInfoDescription={#MyAppName} installer — by LALZIN, with groove and love

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[Types]
Name: "full"; Description: "VST3 plug-in + standalone application"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Common Files\VST3)"; Types: full custom
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#BuildDir}\VST3\{#MyAppName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#MyAppName}.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "{#BuildDir}\Standalone\{#MyAppName}.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone
Source: "..\common\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\common\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\common\LilitaOne-OFL.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppName}.exe"; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#MyAppName}.vst3"

#if Plugin == "BW"
[CustomMessages]
en.LatencyNote=Backwash starts in LIVE mode (zero latency): it listens to the first bar, keeps it in memory and plays its swell before each hit. Turn LIVE off for the exact mode on any sound (2-second latency, compensated by your DAW on playback).
fr.LatencyNote=Backwash démarre en mode LIVE (zéro latence) : il écoute la première mesure, la garde en mémoire et rejoue son souffle avant chaque coup. Désactive LIVE pour le mode exact sur n'importe quel son (latence de 2 secondes, compensée par le logiciel en lecture).
#endif

[Messages]
en.WelcomeLabel2=This will install {#MyAppName} {#MyAppVersion} on your computer.%n%nby LALZIN — with groove and love ♥
fr.WelcomeLabel2={#MyAppName} {#MyAppVersion} va être installé sur votre ordinateur.%n%nby LALZIN — with groove and love ♥
en.FinishedLabel={#MyAppName} is installed.%n%nIn Ableton Live: Options > Preferences > Plug-Ins, enable "Use VST3 Plug-In System Folders", then Rescan.%n%nby LALZIN — with groove and love ♥
fr.FinishedLabel={#MyAppName} est installé.%n%nDans Ableton Live : Options > Préférences > Plug-Ins, activez « Use VST3 Plug-In System Folders », puis Rescan.%n%nby LALZIN — with groove and love ♥

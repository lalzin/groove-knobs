; French Filter — Windows installer (Inno Setup 6).
; Built by scripts/make_installer_win.ps1, or:
;   iscc /DMyAppVersion=0.1.0 /DBuildDir=<...>\FrenchFilter_artefacts\Release installer\windows\FrenchFilter.iss

#define MyAppName "French Filter"
#define MyPublisher "LALZIN"
#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build-win\plugins\french_filter\FrenchFilter_artefacts\Release"
#endif

[Setup]
AppId={{052BE9F5-4CAF-4309-A07D-2ED20F62E6A6}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyPublisher}
DefaultDirName={autopf}\{#MyPublisher}\{#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
LicenseFile=..\common\LICENSE.txt
OutputDir=..\..\dist
OutputBaseFilename=FrenchFilter-{#MyAppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName} ({#MyPublisher})
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyPublisher}
VersionInfoDescription={#MyAppName} installer

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
Source: "{#BuildDir}\VST3\French Filter.vst3\*"; DestDir: "{commoncf64}\VST3\French Filter.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "{#BuildDir}\Standalone\French Filter.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone
Source: "..\common\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\common\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\common\LilitaOne-OFL.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\French Filter.exe"; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\French Filter.vst3"

[Messages]
FinishedLabel=French Filter is installed.%n%nIn Ableton Live: Options > Preferences > Plug-Ins, enable "Use VST3 Plug-In System Folders", then Rescan.

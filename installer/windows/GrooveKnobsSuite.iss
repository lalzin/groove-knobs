; Groove Knobs — Windows installer for the whole suite (Inno Setup 6).
; Built by scripts/make_installer_win.ps1 (-Plugin All or Suite), or:
;   iscc /DMyAppVersion=0.1.6 /DBuildRoot=<...>\build-win\plugins installer\windows\GrooveKnobsSuite.iss
; One component per plug-in (VST3 + standalone), all selected; the same files
; and folders as the single installers, so either can update the other.

#define MyAppName "Groove Knobs"
#define MyPublisher "LALZIN"
#ifndef MyAppVersion
  #define MyAppVersion "0.1.6"
#endif
#ifndef BuildRoot
  #define BuildRoot "..\..\build-win\plugins"
#endif

[Setup]
AppId={{0A499703-FE31-4855-A8B3-24A169C90578}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyPublisher}
DefaultDirName={autopf}\{#MyPublisher}\{#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
LicenseFile=..\common\LICENSE.txt
OutputDir=..\..\dist
OutputBaseFilename=GrooveKnobs-{#MyAppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName} ({#MyPublisher})
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyPublisher}
VersionInfoDescription=Groove Knobs suite installer — by LALZIN, with groove and love

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[Types]
Name: "full"; Description: "The whole suite (VST3 + standalone applications)"
Name: "vst3"; Description: "VST3 plug-ins only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "ff"; Description: "French Filter — the French Touch filter"; Types: full vst3 custom
Name: "ff\vst3"; Description: "VST3"; Types: full vst3 custom
Name: "ff\app"; Description: "Standalone application"; Types: full
Name: "bt"; Description: "Boogie Trio — 70s string-machine triple chorus"; Types: full vst3 custom
Name: "bt\vst3"; Description: "VST3"; Types: full vst3 custom
Name: "bt\app"; Description: "Standalone application"; Types: full
Name: "bw"; Description: "Backwash — tempo-synced reverse reverb"; Types: full vst3 custom
Name: "bw\vst3"; Description: "VST3"; Types: full vst3 custom
Name: "bw\app"; Description: "Standalone application"; Types: full
Name: "pp"; Description: "Pew Pew — the disco pew (instrument)"; Types: full vst3 custom
Name: "pp\vst3"; Description: "VST3"; Types: full vst3 custom
Name: "pp\app"; Description: "Standalone application"; Types: full
Name: "sib"; Description: "Sing It Back — a crowd singing with your voice"; Types: full vst3 custom
Name: "sib\vst3"; Description: "VST3"; Types: full vst3 custom
Name: "sib\app"; Description: "Standalone application"; Types: full
Name: "kl"; Description: "Kick Lock — the sidechain that locks the sub on every kick (+ Kick Lock Send)"; Types: full vst3 custom
Name: "kl\vst3"; Description: "VST3 (Kick Lock and Kick Lock Send)"; Types: full vst3 custom
Name: "kl\app"; Description: "Standalone application"; Types: full
Name: "sl"; Description: "Slapper — the funk bass, one knob (instrument)"; Types: full vst3 custom
Name: "sl\vst3"; Description: "VST3"; Types: full vst3 custom
Name: "sl\app"; Description: "Standalone application"; Types: full

[Files]
Source: "{#BuildRoot}\french_filter\FrenchFilter_artefacts\Release\VST3\French Filter.vst3\*"; DestDir: "{commoncf64}\VST3\French Filter.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: ff\vst3
Source: "{#BuildRoot}\french_filter\FrenchFilter_artefacts\Release\Standalone\French Filter.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: ff\app
Source: "{#BuildRoot}\boogie_trio\BoogieTrio_artefacts\Release\VST3\Boogie Trio.vst3\*"; DestDir: "{commoncf64}\VST3\Boogie Trio.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: bt\vst3
Source: "{#BuildRoot}\boogie_trio\BoogieTrio_artefacts\Release\Standalone\Boogie Trio.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: bt\app
Source: "{#BuildRoot}\backwash\Backwash_artefacts\Release\VST3\Backwash.vst3\*"; DestDir: "{commoncf64}\VST3\Backwash.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: bw\vst3
Source: "{#BuildRoot}\backwash\Backwash_artefacts\Release\Standalone\Backwash.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: bw\app
Source: "{#BuildRoot}\pew_pew\PewPew_artefacts\Release\VST3\Pew Pew.vst3\*"; DestDir: "{commoncf64}\VST3\Pew Pew.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: pp\vst3
Source: "{#BuildRoot}\pew_pew\PewPew_artefacts\Release\Standalone\Pew Pew.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: pp\app
Source: "{#BuildRoot}\sing_it_back\SingItBack_artefacts\Release\VST3\Sing It Back.vst3\*"; DestDir: "{commoncf64}\VST3\Sing It Back.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: sib\vst3
Source: "{#BuildRoot}\sing_it_back\SingItBack_artefacts\Release\Standalone\Sing It Back.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: sib\app
Source: "{#BuildRoot}\kick_lock\KickLock_artefacts\Release\VST3\Kick Lock.vst3\*"; DestDir: "{commoncf64}\VST3\Kick Lock.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: kl\vst3
Source: "{#BuildRoot}\kick_lock_send\KickLockSend_artefacts\Release\VST3\Kick Lock Send.vst3\*"; DestDir: "{commoncf64}\VST3\Kick Lock Send.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: kl\vst3
Source: "{#BuildRoot}\kick_lock\KickLock_artefacts\Release\Standalone\Kick Lock.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: kl\app
Source: "{#BuildRoot}\slapper\Slapper_artefacts\Release\VST3\Slapper.vst3\*"; DestDir: "{commoncf64}\VST3\Slapper.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: sl\vst3
Source: "{#BuildRoot}\slapper\Slapper_artefacts\Release\Standalone\Slapper.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: sl\app
Source: "..\common\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\common\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\common\LilitaOne-OFL.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Groove Knobs\French Filter"; Filename: "{app}\French Filter.exe"; Components: ff\app
Name: "{autoprograms}\Groove Knobs\Boogie Trio"; Filename: "{app}\Boogie Trio.exe"; Components: bt\app
Name: "{autoprograms}\Groove Knobs\Backwash"; Filename: "{app}\Backwash.exe"; Components: bw\app
Name: "{autoprograms}\Groove Knobs\Pew Pew"; Filename: "{app}\Pew Pew.exe"; Components: pp\app
Name: "{autoprograms}\Groove Knobs\Sing It Back"; Filename: "{app}\Sing It Back.exe"; Components: sib\app
Name: "{autoprograms}\Groove Knobs\Kick Lock"; Filename: "{app}\Kick Lock.exe"; Components: kl\app
Name: "{autoprograms}\Groove Knobs\Slapper"; Filename: "{app}\Slapper.exe"; Components: sl\app

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\French Filter.vst3"; Components: ff\vst3
Type: filesandordirs; Name: "{commoncf64}\VST3\Boogie Trio.vst3"; Components: bt\vst3
Type: filesandordirs; Name: "{commoncf64}\VST3\Backwash.vst3"; Components: bw\vst3
Type: filesandordirs; Name: "{commoncf64}\VST3\Pew Pew.vst3"; Components: pp\vst3
Type: filesandordirs; Name: "{commoncf64}\VST3\Sing It Back.vst3"; Components: sib\vst3
Type: filesandordirs; Name: "{commoncf64}\VST3\Kick Lock.vst3"; Components: kl\vst3
Type: filesandordirs; Name: "{commoncf64}\VST3\Kick Lock Send.vst3"; Components: kl\vst3
Type: filesandordirs; Name: "{commoncf64}\VST3\Slapper.vst3"; Components: sl\vst3

[Messages]
en.WelcomeLabel2=This will install the Groove Knobs suite {#MyAppVersion} on your computer: French Filter, Boogie Trio, Backwash, Pew Pew, Sing It Back, Kick Lock and Slapper (choose them on the next pages).%n%nby LALZIN — with groove and love ♥
fr.WelcomeLabel2=La suite Groove Knobs {#MyAppVersion} va être installée sur votre ordinateur : French Filter, Boogie Trio, Backwash, Pew Pew, Sing It Back, Kick Lock et Slapper (à choisir aux étapes suivantes).%n%nby LALZIN — with groove and love ♥
en.FinishedLabel=Groove Knobs is installed.%n%nIn Ableton Live: Options > Preferences > Plug-Ins, enable "Use VST3 Plug-In System Folders", then Rescan. Pew Pew and Slapper are instruments (MIDI track). Kick Lock goes on the bass, Kick Lock Send on the kick's track.%n%nby LALZIN — with groove and love ♥
fr.FinishedLabel=Groove Knobs est installé.%n%nDans Ableton Live : Options > Préférences > Plug-Ins, activez « Use VST3 Plug-In System Folders », puis Rescan. Pew Pew et Slapper sont des instruments (piste MIDI). Kick Lock va sur la basse, Kick Lock Send sur la piste du kick.%n%nby LALZIN — with groove and love ♥

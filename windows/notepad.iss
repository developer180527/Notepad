; Inno Setup script for Notepad.
;
; Produces a normal Windows installer: Program Files by default, Start Menu
; entry, optional desktop icon, an uninstaller, and .note file association.
; Built in CI as:
;   ISCC /DAppVersion=3.1.0 /DSourceDir=dist windows\notepad.iss
;
; The app also self-registers its association on first launch (winregister.cpp);
; doing it here as well means it works for every user of the machine and, more
; importantly, is cleaned up properly on uninstall.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "dist"
#endif

#define AppName "Notepad"
#define AppPublisher "Notepad"
#define AppExeName "Notepad.exe"

[Setup]
AppId={{8E4F2A16-3D5C-4B7E-9F21-6C0A5D3E8B14}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Installs per-machine when run as admin, per-user otherwise — no UAC prompt
; for a user who just wants it in their own profile.
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=.
OutputBaseFilename=Notepad-windows-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; The app is 64-bit only; refuse rather than install something that cannot run.
; "x64" (not "x64compatible") so the script also compiles with Inno Setup < 6.3,
; since the CI installs whatever Chocolatey currently ships.
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayIcon={app}\{#AppExeName}
SetupIconFile=notepad.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associatenote"; Description: "Open .note files with {#AppName}"; GroupDescription: "File associations:"

[Files]
; Everything windeployqt staged: the exe, Qt DLLs, plugins and the thumbnail
; handler. recursesubdirs keeps the plugin directory layout Qt expects.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
; .note association. Written under the same root the install used, so a
; per-user install does not need admin rights and uninstalls cleanly.
Root: HKA; Subkey: "Software\Classes\.note"; ValueType: string; ValueName: ""; ValueData: "Notepad.note"; Flags: uninsdeletevalue; Tasks: associatenote
Root: HKA; Subkey: "Software\Classes\Notepad.note"; ValueType: string; ValueName: ""; ValueData: "Notepad Document"; Flags: uninsdeletekey; Tasks: associatenote
Root: HKA; Subkey: "Software\Classes\Notepad.note\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExeName},0"; Tasks: associatenote
Root: HKA; Subkey: "Software\Classes\Notepad.note\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""; Tasks: associatenote
; Offer Notepad in "Open with" for the plain-text formats it handles, without
; stealing the default from whatever the user already uses.
Root: HKA; Subkey: "Software\Classes\.txt\OpenWithProgids"; ValueType: string; ValueName: "Notepad.note"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.md\OpenWithProgids"; ValueType: string; ValueName: "Notepad.note"; ValueData: ""; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; windeployqt writes a cache next to the exe at runtime; remove the folder if
; nothing else is left in it.
Type: dirifempty; Name: "{app}"

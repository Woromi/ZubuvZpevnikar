; The Windows installer of Zpěvníkář (Inno Setup 6), made by package.ps1:
;   ISCC /DAppVersion=<x.y.z> /DAppDir=<the installed app: bin\, share\> /O<out dir> zpevnikar.iss
; Installs into Program Files (or the user's folder, when they choose so), with a Start menu entry,
; an optional desktop icon and an uninstaller. UTF-8 with BOM: Inno Setup reads the names so.

#ifndef AppVersion
  #error Pass /DAppVersion=x.y.z
#endif
#ifndef AppDir
  #error Pass /DAppDir=<folder with bin\zpevnikar.exe>
#endif

[Setup]
AppId={{FBD03D02-07A1-4F7C-BB21-C7EAE0BE7801}
AppName=Zpěvníkář
AppVersion={#AppVersion}
AppVerName=Zpěvníkář {#AppVersion}
AppPublisher=Modrozoubek
AppPublisherURL=https://github.com/Woromi/ZubuvZpevnikar
DefaultDirName={autopf}\Zpevnikar
DefaultGroupName=Zpěvníkář
DisableProgramGroupPage=yes
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=Zpevnikar-{#AppVersion}-setup
SetupIconFile=..\..\app\icons\zpevnikar.ico
UninstallDisplayIcon={app}\bin\zpevnikar.exe
LicenseFile=..\..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "cs"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#AppDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Zpěvníkář"; Filename: "{app}\bin\zpevnikar.exe"; Comment: "Edit the songs of Zubův Zpěvník and put songbooks together"
Name: "{autodesktop}\Zpěvníkář"; Filename: "{app}\bin\zpevnikar.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\zpevnikar.exe"; Description: "{cm:LaunchProgram,Zpěvníkář}"; Flags: nowait postinstall skipifsilent

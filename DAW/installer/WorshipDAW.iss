; Instalador de Worship DAW para Windows (Inno Setup 6).
; Lo usa scripts/package-windows.ps1, que pasa AppVersion, SourceDir y OutputDir.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\WorshipDAW_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
AppId={{6E1C5B1E-8A51-4C0B-9C6B-5E8B7D2F4A10}
AppName=Worship DAW
AppVersion={#AppVersion}
AppPublisher=Worship Sounds
DefaultDirName={autopf}\Worship DAW
DefaultGroupName=Worship DAW
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=WorshipDAW-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayIcon={app}\Worship DAW.exe
; Una actualización reemplaza la versión anterior (los proyectos están en Documentos y no se tocan)
CloseApplications=yes

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\Worship DAW.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Worship DAW"; Filename: "{app}\Worship DAW.exe"
Name: "{autodesktop}\Worship DAW"; Filename: "{app}\Worship DAW.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Worship DAW.exe"; Description: "{cm:LaunchProgram,Worship DAW}"; Flags: nowait postinstall skipifsilent

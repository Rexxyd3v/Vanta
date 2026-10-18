#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #error SourceDir must point to the staged game files
#endif
#ifndef ReleaseDir
  #define ReleaseDir "..\bin\release"
#endif

[Setup]
AppId={{2F03930D-47CB-466C-9973-222D2A2B6C20}
AppName=VANTA
AppVersion={#AppVersion}
AppPublisher=R Gamestudio
AppPublisherURL=https://github.com/Rexxyd3v/Vanta
AppSupportURL=https://github.com/Rexxyd3v/Vanta/issues
DefaultDirName={localappdata}\Programs\VANTA
DefaultGroupName=VANTA
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#ReleaseDir}
OutputBaseFilename=VANTA-{#AppVersion}-Setup
SetupIconFile=vanta.ico
UninstallDisplayIcon={app}\Vanta.exe
Compression=lzma2/normal
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
SetupLogging=yes
VersionInfoVersion={#AppVersion}
VersionInfoCompany=R Gamestudio
VersionInfoDescription=VANTA Windows Installer

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\Vanta.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\BUILD-INFO.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\VANTA"; Filename: "{app}\Vanta.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\VANTA"; Filename: "{app}\Vanta.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Vanta.exe"; WorkingDir: "{app}"; Description: "Launch VANTA"; Flags: nowait postinstall skipifsilent

; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 AdonisZeng
;
; 由 scripts\build-release.ps1 调用：
;   ISCC.exe /DAppVersion=0.1.0 /DStageDir=dist\Baud-0.1.0-portable-x64 Baud.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

#ifndef StageDir
  #define StageDir "dist\portable"
#endif

[Setup]
AppId=Baud
AppName=Baud
AppVersion={#AppVersion}
AppVerName=Baud {#AppVersion}
AppPublisher=AdonisZeng
AppPublisherURL=https://github.com/AdonisZeng/Baud
AppSupportURL=https://github.com/AdonisZeng/Baud/issues
AppUpdatesURL=https://github.com/AdonisZeng/Baud/releases
DefaultDirName={autopf}\Baud
DisableProgramGroupPage=yes
LicenseFile=LICENSE
OutputDir=dist
OutputBaseFilename=Baud-Setup-{#AppVersion}
SetupIconFile=resources\Baud.ico
UninstallDisplayIcon={app}\Baud.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
UsePreviousPrivileges=yes
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
VersionInfoVersion={#AppVersion}
VersionInfoCompany=AdonisZeng
VersionInfoDescription=Baud VOFA Serial Host
VersionInfoProductName=Baud
VersionInfoProductVersion={#AppVersion}
VersionInfoCopyright=Copyright (C) 2026 AdonisZeng
SourceDir=..

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Baud"; Filename: "{app}\Baud.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Baud"; Filename: "{app}\Baud.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Baud.exe"; Description: "{cm:LaunchProgram,Baud}"; Flags: nowait postinstall skipifsilent

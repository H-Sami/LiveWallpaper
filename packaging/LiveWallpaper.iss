#define MyAppName "LiveWallpaper"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "LiveWallpaper"
#define MyAppExeName "LiveWallpaper.exe"
#define MyHostExeName "LiveWallpaper.Host.exe"

[Setup]
AppId={{17CDA9E1-5B04-43B4-85F5-73D85BE143B2}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\LiveWallpaper
DefaultGroupName=LiveWallpaper
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=LiveWallpaper-Setup-x64
SetupIconFile=..\controller\Assets\LiveWallpaper.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
CloseApplications=no
CloseApplicationsFilter={#MyAppExeName},{#MyHostExeName}
RestartApplications=no
SetupLogging=yes
VersionInfoVersion={#MyAppVersion}.0
VersionInfoProductName={#MyAppName}
VersionInfoDescription=LiveWallpaper x64 Setup
VersionInfoCompany={#MyAppPublisher}
VersionInfoCopyright=Copyright (c) 2026 LiveWallpaper

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startup"; Description: "Start the current wallpaper when I sign in to Windows"; GroupDescription: "Startup:"; Flags: unchecked
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "..\artifacts\stage\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\LiveWallpaper"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\Assets\LiveWallpaper.ico"; IconIndex: 0
Name: "{group}\Uninstall LiveWallpaper"; Filename: "{uninstallexe}"
Name: "{autodesktop}\LiveWallpaper"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\Assets\LiveWallpaper.ico"; IconIndex: 0; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "LiveWallpaper"; ValueData: """{app}\{#MyHostExeName}"" --apply"; Tasks: startup; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch LiveWallpaper"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\LiveWallpaper"

[Code]
procedure SHChangeNotify(wEventId, uFlags: LongWord; dwItem1, dwItem2: Integer);
  external 'SHChangeNotify@shell32.dll stdcall';

procedure StopLiveWallpaper(const InstallDirectory: String);
var
  ResultCode: Integer;
  HostPath: String;
begin
  HostPath := AddBackslash(InstallDirectory) + '{#MyHostExeName}';
  if FileExists(HostPath) then
    Exec(HostPath, '--exit', InstallDirectory, SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM {#MyAppExeName}', '', SW_HIDE,
       ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopLiveWallpaper(ExpandConstant('{app}'));
  Result := '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    SHChangeNotify($08000000, 0, 0, 0);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    StopLiveWallpaper(ExpandConstant('{app}'));
end;

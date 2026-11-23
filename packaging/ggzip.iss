#ifndef AppVersion
  #error AppVersion must be supplied by build.ps1
#endif
#ifndef DistDir
  #error DistDir must be supplied by build.ps1
#endif
#ifndef OutputDir
  #error OutputDir must be supplied by build.ps1
#endif
#ifndef PackageName
  #error PackageName must be supplied by build.ps1
#endif

[Setup]
AppId={{4E828A25-22C8-4B75-AD12-EE06934C4DD7}
AppName=ggZip
AppVersion={#AppVersion}
AppPublisher=ggZip
AppPublisherURL=https://github.com/liuyb4016/ggZip
AppSupportURL=https://github.com/liuyb4016/ggZip/issues
DefaultDirName={localappdata}\Programs\ggZip
DefaultGroupName=ggZip
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename={#PackageName}-setup
SetupIconFile=..\shell\res\ggzip.ico
UninstallDisplayIcon={app}\ggzip_gui.exe
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "associations"; Description: "Add ggZip to Open with (keep existing default apps)"
Name: "shell"; Description: "Install classic Explorer context menu"

[Files]
Source: "{#DistDir}\*"; DestDir: "{app}"; Excludes: "ggzip_shell.dll"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DistDir}\ggzip_shell.dll"; DestDir: "{app}"; Flags: ignoreversion regserver restartreplace uninsrestartdelete; Tasks: shell

[Registry]
Root: HKCU; Subkey: "Software\ggZip"; ValueType: string; ValueName: "GgzPath"; ValueData: "{app}\ggz.exe"; Flags: uninsdeletevalue; Tasks: shell

[Icons]
Name: "{group}\ggZip"; Filename: "{app}\ggzip_gui.exe"
Name: "{group}\Uninstall ggZip"; Filename: "{uninstallexe}"
Name: "{autodesktop}\ggZip"; Filename: "{app}\ggzip_gui.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\ggzip_gui.exe"; Description: "Launch ggZip"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\ggzip_gui.exe"; Parameters: "--unregister-associations --quiet"; Flags: runhidden waituntilterminated; RunOnceId: "FileAssociations"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('associations') then
  begin
    if not Exec(ExpandConstant('{app}\ggzip_gui.exe'),
      '--register-associations --quiet', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
      RaiseException('Unable to start ggZip to register file associations.');
    if ResultCode <> 0 then
      RaiseException('File association registration failed. Exit code: ' + IntToStr(ResultCode));
  end;
end;

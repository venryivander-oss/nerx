; nrxl.iss: installer nrxl (Inno Setup 6). Dibuat lewat installer\buat.bat, yang
; mengisi installer\paket dulu (siapkan.ps1). Hasilnya installer\out\nrxl-setup.exe.

#define AppVer "0.1.0"

[Setup]
AppId={{7B1E4C2A-9D3F-4E8B-A6C1-5F2D8E0B3A71}
AppName=nrxl
AppVersion={#AppVer}
AppPublisher=venryivander
DefaultDirName={autopf}\nrxl
DefaultGroupName=nrxl
DisableProgramGroupPage=yes
; boleh dipasang untuk satu pemakai saja (tanpa admin) atau untuk semua
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
LicenseFile=paket\LICENSE
OutputDir=out
OutputBaseFilename=nrxl-setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesEnvironment=yes
UninstallDisplayName=nrxl {#AppVer}

[Tasks]
Name: "addpath"; Description: "Add nrxlang to PATH (use it from any folder)"

[Files]
Source: "paket\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Code]
// PATH milik pemakai (install per-user) atau sistem (install untuk semua)
function PathRoot: Integer;
begin
  if IsAdminInstallMode then Result := HKEY_LOCAL_MACHINE else Result := HKEY_CURRENT_USER;
end;

function PathKey: String;
begin
  if IsAdminInstallMode then
    Result := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else
    Result := 'Environment';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Path, Dir: String;
begin
  if (CurStep <> ssPostInstall) or not WizardIsTaskSelected('addpath') then Exit;
  Dir := ExpandConstant('{app}');
  if not RegQueryStringValue(PathRoot, PathKey, 'Path', Path) then Path := '';
  if Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Path) + ';') > 0 then Exit;
  if (Path <> '') and (Path[Length(Path)] <> ';') then Path := Path + ';';
  RegWriteExpandStringValue(PathRoot, PathKey, 'Path', Path + Dir);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Path, Dir: String;
  P: Integer;
begin
  if CurUninstallStep <> usPostUninstall then Exit;
  if not RegQueryStringValue(PathRoot, PathKey, 'Path', Path) then Exit;
  Dir := ExpandConstant('{app}');
  Path := ';' + Path + ';';
  P := Pos(';' + Uppercase(Dir) + ';', Uppercase(Path));
  if P = 0 then Exit;
  Delete(Path, P, Length(Dir) + 1);
  Path := Copy(Path, 2, Length(Path) - 2);
  RegWriteExpandStringValue(PathRoot, PathKey, 'Path', Path);
end;

; The double-click Windows installer (Inno Setup 6). Not compiled directly: installer/windows/build-setup.sh
; copies this file next to the generated components.iss, files.iss and installdelete.iss (one entry per
; built .vst3 bundle) and runs iscc with:
;   /DAppVersion=0.12.0  /DPluginList=Deepr,Dropr,...  /DRetiredList=Gently,...  [/DSign]
; The plug-ins go into C:\Program Files\Common Files\VST3\bfielstr for all users, one component (tick
; box) per plug-in. Settings > Apps lists the suite with an uninstaller.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef PluginList
  #error PluginList must be defined (build-setup.sh does this)
#endif
#ifndef RetiredList
  #define RetiredList ""
#endif

[Setup]
; Never change AppId: it is how a new version finds and upgrades the installed one.
AppId={{8804C6F2-6057-4889-BBE2-D199E43D020B}
AppName=Audio plug-ins (bfielstr)
AppVersion={#AppVersion}
AppVerName=Audio plug-ins (bfielstr) {#AppVersion}
AppPublisher=bfielstr
AppPublisherURL=https://github.com/bfielstr/audio-plugins
AppSupportURL=https://github.com/bfielstr/audio-plugins/issues
AppUpdatesURL=https://github.com/bfielstr/audio-plugins/releases
DefaultDirName={commoncf64}\VST3\bfielstr
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
; the uninstaller lives outside the VST3 folder, so hosts scanning it see only plug-ins
UninstallFilesDir={commonpf64}\bfielstr audio plug-ins
UninstallDisplayName=Audio plug-ins (bfielstr) {#AppVersion}
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=LICENSE.txt
InfoBeforeFile=welcome.txt
OutputDir=output
OutputBaseFilename=Plugins-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
#ifdef Sign
; build-setup.sh passes the sign command with /Ssigntool=... when a certificate is configured
SignTool=signtool
SignedUninstaller=yes
#endif

[Types]
Name: "full"; Description: "All plug-ins"
Name: "custom"; Description: "Choose which plug-ins to install"; Flags: iscustom

[Components]
#include "components.iss"

[InstallDelete]
; the older copy of each chosen plug-in goes first, so files an older version had do not linger
#include "installdelete.iss"

[Files]
#include "files.iss"

[UninstallDelete]
Type: dirifempty; Name: "{app}"

[Messages]
SelectComponentsLabel2=Untick any plug-ins you do not want, then click Next.

[Code]
// True if the bundle was made by us (older builds named the vendor "Simplr").
function IsOurs(const Bundle: String): Boolean;
var
  S: AnsiString;
begin
  Result := False;
  if LoadStringFromFile(Bundle + '\Contents\Resources\moduleinfo.json', S) then
    Result := (Pos('"Vendor": "bfielstr"', S) > 0) or (Pos('"Vendor": "Simplr"', S) > 0) or
              (Pos('"Vendor":"bfielstr"', S) > 0) or (Pos('"Vendor":"Simplr"', S) > 0);
end;

procedure RemoveIfOurs(const Bundle, Reason: String);
begin
  if DirExists(Bundle) and IsOurs(Bundle) then
  begin
    Log('Removing ' + Bundle + ' (' + Reason + ')');
    DelTree(Bundle, True, True, True);
  end;
end;

// Takes the first item off a comma-separated list.
function NextItem(var List: String): String;
var
  P: Integer;
begin
  P := Pos(',', List);
  if P = 0 then
  begin
    Result := List;
    List := '';
  end else
  begin
    Result := Copy(List, 1, P - 1);
    Delete(List, 1, P);
  end;
end;

// Before installing: copies of ours that a host would list a second time. Very old installers put the
// bundles straight into the VST3 folder; scripts/install.ps1 without Administrator rights puts them in
// the user's own VST3 folder. Bundles the suite renamed go too. Other vendors' bundles are never touched.
procedure CurStepChanged(CurStep: TSetupStep);
var
  List, Name, SysRoot, UserRoot: String;
begin
  if CurStep <> ssInstall then
    Exit;
  SysRoot := ExpandConstant('{commoncf64}\VST3');
  UserRoot := ExpandConstant('{localappdata}\Programs\Common\VST3');

  List := '{#PluginList}';
  while List <> '' do
  begin
    Name := NextItem(List);
    if WizardIsComponentSelected(Lowercase(Name)) then
    begin
      RemoveIfOurs(SysRoot + '\' + Name + '.vst3', 'old copy outside the bfielstr folder');
      RemoveIfOurs(UserRoot + '\bfielstr\' + Name + '.vst3', 'copy for one user, now installed for all users');
      RemoveIfOurs(UserRoot + '\' + Name + '.vst3', 'copy for one user, now installed for all users');
    end;
  end;

  List := '{#RetiredList}';
  while List <> '' do
  begin
    Name := NextItem(List);
    RemoveIfOurs(SysRoot + '\bfielstr\' + Name + '.vst3', 'renamed');
    RemoveIfOurs(SysRoot + '\' + Name + '.vst3', 'renamed');
    RemoveIfOurs(UserRoot + '\bfielstr\' + Name + '.vst3', 'renamed');
    RemoveIfOurs(UserRoot + '\' + Name + '.vst3', 'renamed');
  end;
end;

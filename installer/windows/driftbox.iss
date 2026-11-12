; Driftbox Windows installer (Inno Setup 6.4+).
; Built by scripts/ci/make_installer_windows.ps1, which passes:
;   /DAppVersion=X.Y.Z         version without the leading "v" and without -rcN
;   /DAppVersionText=X.Y.Z-rc1 full version string (optional, defaults to AppVersion)
;   /DSourceDir=<dir>          unzipped package: Driftbox.vst3\, Driftbox.clap, LICENSE
;   /DOutputDir=<dir>          where the setup exe is written
;   /DOutputName=<name>        base name of the setup exe (no extension)
#ifndef AppVersion
  #error AppVersion must be defined (iscc /DAppVersion=1.2.3 ...)
#endif
#ifndef AppVersionText
  #define AppVersionText AppVersion
#endif
#ifndef SourceDir
  #error SourceDir must be defined
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef OutputName
  #define OutputName "Driftbox-v" + AppVersionText + "-windows-setup"
#endif

[Setup]
; Never change AppId: it identifies the installation for upgrades and uninstall.
AppId={{CF238C55-C87C-4950-9FCD-4B3A7FEE495F}
AppName=Driftbox
AppVersion={#AppVersionText}
AppVerName=Driftbox {#AppVersionText}
AppPublisher=Ivan Erricis
AppPublisherURL=https://github.com/ivanerricis/driftbox
AppSupportURL=https://github.com/ivanerricis/driftbox/issues
AppUpdatesURL=https://github.com/ivanerricis/driftbox/releases
VersionInfoVersion={#AppVersion}.0
DefaultDirName={autopf}\Driftbox
DefaultGroupName=Driftbox
DisableProgramGroupPage=yes
; Per-machine install: VST3/CLAP go to %CommonProgramFiles%, which needs admin.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#SourceDir}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Driftbox {#AppVersionText}
UninstallFilesDir={autopf}\Driftbox
CloseApplications=no

[Types]
Name: "full"; Description: "Full installation (VST3, CLAP)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom
Name: "clap"; Description: "CLAP plug-in"; Types: full custom

[Files]
; VST3 is a bundle directory on Windows.
Source: "{#SourceDir}\Driftbox.vst3\*"; DestDir: "{commoncf64}\VST3\Driftbox.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Driftbox.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
; Licence is always installed next to the uninstaller.
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

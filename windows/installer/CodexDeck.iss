#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif

[Setup]
AppId={{A6EAF76F-AAD8-4D36-92E9-C358E6AC7493}
AppName=PanPal
AppVersion={#AppVersion}
AppPublisher=Local custom build
DefaultDirName={autopf}\PanPal
DefaultGroupName=PanPal
OutputDir=..\..\dist\installer
OutputBaseFilename=PanPal-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
CloseApplications=yes
CloseApplicationsFilter=CardBridge.exe
RestartApplications=no

[InstallDelete]
; Branding changed in 1.4.0.  Remove both exact legacy/current startup links
; before recreating one canonical PanPal entry, otherwise two installations
; can race to bind TCP 7788 after logon.
Type: files; Name: "{userstartup}\CodexDeck.lnk"
Type: files; Name: "{userstartup}\Codex Deck.lnk"
Type: files; Name: "{userstartup}\CardBridge.lnk"
Type: files; Name: "{userstartup}\PanPal.lnk"
Type: files; Name: "{commonstartup}\CodexDeck.lnk"
Type: files; Name: "{commonstartup}\Codex Deck.lnk"
Type: files; Name: "{commonstartup}\CardBridge.lnk"
Type: files; Name: "{commonstartup}\PanPal.lnk"

[Files]
Source: "..\..\dist\windows\CardBridge\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion
Source: "..\README-Windows.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\PanPal"; Filename: "{app}\CardBridge.exe"
Name: "{group}\Windows setup guide"; Filename: "{app}\README-Windows.md"
Name: "{userstartup}\PanPal"; Filename: "{app}\CardBridge.exe"; WorkingDir: "{app}"

[Run]
; An older differently-named install directory may not be registered with
; Restart Manager.  End that exact legacy process before launching the new
; build; cmd always returns success when no previous process exists.
Filename: "{cmd}"; Parameters: "/c taskkill /F /IM CardBridge.exe >nul 2>&1 & exit /b 0"; Flags: runhidden waituntilterminated
Filename: "{cmd}"; Parameters: "/c netsh advfirewall firewall add rule name=""PanPal TCP"" dir=in action=allow protocol=TCP localport=7788 profile=private"; Flags: runhidden
Filename: "{cmd}"; Parameters: "/c netsh advfirewall firewall add rule name=""PanPal UDP"" dir=in action=allow protocol=UDP localport=7789 profile=private"; Flags: runhidden
Filename: "{app}\CardBridge.exe"; Description: "Start PanPal"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{cmd}"; Parameters: "/c netsh advfirewall firewall delete rule name=""PanPal TCP"""; Flags: runhidden; RunOnceId: "RemovePanPalTcpFirewall"
Filename: "{cmd}"; Parameters: "/c netsh advfirewall firewall delete rule name=""PanPal UDP"""; Flags: runhidden; RunOnceId: "RemovePanPalUdpFirewall"

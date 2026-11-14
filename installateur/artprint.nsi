; ArtPrint Slicer - licence AGPL v3
; Script NSIS de l'installateur Windows 64 bits.
; Utilisation :
;   makensis -INPUTCHARSET UTF8 -DSOURCE=<dossier des fichiers> -DVERSION=0.1.0 -DSORTIE=<fichier.exe> artprint.nsi

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel admin

!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef SORTIE
  !define SORTIE "ArtPrintSlicer-Setup-${VERSION}.exe"
!endif

!define NOM "ArtPrint Slicer"
!define EDITEUR "ArtPrint"
!define CLE_DESINSTALLATION "Software\Microsoft\Windows\CurrentVersion\Uninstall\ArtPrintSlicer"
; Identifiant du composant Microsoft Edge WebView2 dans le registre.
!define CLE_WEBVIEW2 "Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"

Name "${NOM} ${VERSION}"
OutFile "${SORTIE}"
InstallDir "$PROGRAMFILES64\${NOM}"
InstallDirRegKey HKLM "${CLE_DESINSTALLATION}" "InstallLocation"
BrandingText "${NOM} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${NOM}"
VIAddVersionKey "FileDescription" "Installateur d'${NOM}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Licence GNU AGPL v3"

!define MUI_ABORTWARNING
; L'icône de l'installateur et du désinstallateur : celle du logiciel.
!define MUI_ICON "${__FILEDIR__}\artprint.ico"
!define MUI_UNICON "${__FILEDIR__}\artprint.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\ArtPrintSlicer.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Lancer ${NOM}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SOURCE}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "French"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "${NOM} fonctionne uniquement sur Windows 64 bits."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

Section "${NOM}" SectionPrincipale
  SectionIn RO
  SetShellVarContext all
  SetOutPath "$INSTDIR"
  ; Une ancienne interface ne doit pas se mélanger avec la nouvelle.
  RMDir /r "$INSTDIR\ui"
  File /r "${SOURCE}\*.*"

  WriteUninstaller "$INSTDIR\Desinstaller.exe"

  CreateDirectory "$SMPROGRAMS\${NOM}"
  CreateShortcut "$SMPROGRAMS\${NOM}\${NOM}.lnk" "$INSTDIR\ArtPrintSlicer.exe"
  CreateShortcut "$SMPROGRAMS\${NOM}\Désinstaller ${NOM}.lnk" "$INSTDIR\Desinstaller.exe"
  CreateShortcut "$DESKTOP\${NOM}.lnk" "$INSTDIR\ArtPrintSlicer.exe"

  ; Entrée dans « Applications installées » de Windows.
  WriteRegStr HKLM "${CLE_DESINSTALLATION}" "DisplayName" "${NOM}"
  WriteRegStr HKLM "${CLE_DESINSTALLATION}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${CLE_DESINSTALLATION}" "Publisher" "${EDITEUR}"
  WriteRegStr HKLM "${CLE_DESINSTALLATION}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${CLE_DESINSTALLATION}" "DisplayIcon" "$INSTDIR\ArtPrintSlicer.exe"
  WriteRegStr HKLM "${CLE_DESINSTALLATION}" "UninstallString" '"$INSTDIR\Desinstaller.exe"'
  WriteRegStr HKLM "${CLE_DESINSTALLATION}" "QuietUninstallString" '"$INSTDIR\Desinstaller.exe" /S'
  WriteRegDWORD HKLM "${CLE_DESINSTALLATION}" "NoModify" 1
  WriteRegDWORD HKLM "${CLE_DESINSTALLATION}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${CLE_DESINSTALLATION}" "EstimatedSize" "$0"

  ; Le composant Edge WebView2 affiche l'interface. Il est présent sur
  ; Windows 11 et sur presque tous les Windows 10 : on prévient s'il manque.
  SetRegView 32
  ReadRegStr $0 HKLM "SOFTWARE\${CLE_WEBVIEW2}" "pv"
  SetRegView 64
  ${If} $0 == ""
    ReadRegStr $0 HKLM "SOFTWARE\${CLE_WEBVIEW2}" "pv"
  ${EndIf}
  ${If} $0 == ""
    ReadRegStr $0 HKCU "Software\${CLE_WEBVIEW2}" "pv"
  ${EndIf}
  ${If} $0 == ""
  ${OrIf} $0 == "0.0.0.0"
    IfSilent +2
    MessageBox MB_OK|MB_ICONINFORMATION "Le composant « Microsoft Edge WebView2 Runtime » semble absent de cet ordinateur.$\r$\n$\r$\n${NOM} en a besoin pour afficher sa fenêtre. Il s'installe gratuitement depuis le site de Microsoft (chercher « WebView2 Runtime »)."
  ${EndIf}
SectionEnd

Section "Uninstall"
  SetShellVarContext all
  Delete "$DESKTOP\${NOM}.lnk"
  RMDir /r "$SMPROGRAMS\${NOM}"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "${CLE_DESINSTALLATION}"
SectionEnd

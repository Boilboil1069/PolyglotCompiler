; ===========================================================================
; PolyglotCompiler — NSIS Installer Script
;
; This script is invoked by package_windows.ps1 with the following defines:
;   /DPRODUCT_VERSION=1.0.0
;   /DSTAGE_DIR=<path-to-staged-directory>
;   /DOUTPUT_FILE=<path-to-output-installer.exe>
;
; Requirements:
;   - NSIS 3.x (https://nsis.sourceforge.io/)
; ===========================================================================

!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "1.0.0"
!endif

!ifndef STAGE_DIR
  !error "STAGE_DIR must be defined — pass /DSTAGE_DIR=<path>"
!endif

!ifndef OUTPUT_FILE
  !define OUTPUT_FILE "PolyglotCompiler-${PRODUCT_VERSION}-windows-x64-setup.exe"
!endif

; ---------------------------------------------------------------------------
; General attributes
; ---------------------------------------------------------------------------
Name "PolyglotCompiler ${PRODUCT_VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$PROGRAMFILES64\PolyglotCompiler"
InstallDirRegKey HKLM "Software\PolyglotCompiler" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

; ---------------------------------------------------------------------------
; Version information embedded in the .exe
; ---------------------------------------------------------------------------
VIProductVersion "${PRODUCT_VERSION}.0"
VIAddVersionKey "ProductName"     "PolyglotCompiler"
VIAddVersionKey "ProductVersion"  "${PRODUCT_VERSION}"
VIAddVersionKey "CompanyName"     "PolyglotCompiler Team"
VIAddVersionKey "LegalCopyright"  "Copyright (c) 2026 PolyglotCompiler Team"
VIAddVersionKey "FileDescription" "PolyglotCompiler Installer"
VIAddVersionKey "FileVersion"     "${PRODUCT_VERSION}"

; ---------------------------------------------------------------------------
; Modern UI 2
; ---------------------------------------------------------------------------
!include "MUI2.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Installer pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE_DIR}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Language
!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------------------
; Sections
; ---------------------------------------------------------------------------
Section "Core Tools (required)" SecCore
    SectionIn RO ; read-only — always installed

    SetOutPath "$INSTDIR\bin"

    ; Copy all files from the staged bin directory
    File /r "${STAGE_DIR}\bin\*.*"

    ; Copy top-level files
    SetOutPath "$INSTDIR"
    File "${STAGE_DIR}\README.md"
    File "${STAGE_DIR}\LICENSE"

    ; Write registry keys for uninstaller and install path
    WriteRegStr HKLM "Software\PolyglotCompiler" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PolyglotCompiler" \
        "DisplayName" "PolyglotCompiler ${PRODUCT_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PolyglotCompiler" \
        "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PolyglotCompiler" \
        "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PolyglotCompiler" \
        "Publisher" "PolyglotCompiler Team"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PolyglotCompiler" \
        "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PolyglotCompiler" \
        "NoRepair" 1

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Add to PATH" SecPath
    ; Add bin directory to system PATH
    ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
    StrCpy $0 "$0;$INSTDIR\bin"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$0"

    ; Broadcast environment change
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
SectionEnd

Section "Start Menu Shortcuts" SecStartMenu
    CreateDirectory "$SMPROGRAMS\PolyglotCompiler"

    ; polyui shortcut (if present)
    IfFileExists "$INSTDIR\bin\polyui.exe" 0 +2
        CreateShortcut "$SMPROGRAMS\PolyglotCompiler\PolyglotCompiler IDE.lnk" \
            "$INSTDIR\bin\polyui.exe" "" "$INSTDIR\bin\polyui.exe" 0

    CreateShortcut "$SMPROGRAMS\PolyglotCompiler\Uninstall.lnk" \
        "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
SectionEnd

; ---------------------------------------------------------------------------
; Section descriptions
; ---------------------------------------------------------------------------
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} \
        "Core compiler tools (polyc, polyld, polyasm, polyopt, polyrt, polybench) and IDE (polyui)."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecPath} \
        "Add PolyglotCompiler bin directory to the system PATH so tools can be used from any terminal."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} \
        "Create Start Menu shortcuts for the IDE and uninstaller."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------------------------------------------------------------------------
; Uninstaller
; ---------------------------------------------------------------------------
Section "Uninstall"
    ; Remove files
    RMDir /r "$INSTDIR\bin"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"

    ; Remove Start Menu shortcuts
    Delete "$SMPROGRAMS\PolyglotCompiler\PolyglotCompiler IDE.lnk"
    Delete "$SMPROGRAMS\PolyglotCompiler\Uninstall.lnk"
    RMDir "$SMPROGRAMS\PolyglotCompiler"

    ; Remove PATH entry
    ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
    ; Simple removal — replace ";$INSTDIR\bin" with ""
    ${WordReplace} $0 ";$INSTDIR\bin" "" "+" $0
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$0"
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000

    ; Remove registry keys
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PolyglotCompiler"
    DeleteRegKey HKLM "Software\PolyglotCompiler"
SectionEnd

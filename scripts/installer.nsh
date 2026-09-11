; Runs in the installer's .onInit, after $INSTDIR is resolved from the previous
; install and before that install's files are removed. Older versions kept the
; user-edited config.json in the install directory, where every update wiped
; it. Copy it to the per-user location (Electron's userData dir) the app reads
; now, unless a config already exists there.
!macro customInit
  ${If} ${FileExists} "$INSTDIR\config.json"
    ${IfNot} ${FileExists} "$APPDATA\Web2NDI\config.json"
      CreateDirectory "$APPDATA\Web2NDI"
      CopyFiles /SILENT "$INSTDIR\config.json" "$APPDATA\Web2NDI\config.json"
    ${EndIf}
  ${EndIf}
!macroend

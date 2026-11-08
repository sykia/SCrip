!include "FileFunc.nsh"
Unicode true
RequestExecutionLevel user
ManifestDPIAware true
!include "MUI2.nsh"
!include "x64.nsh"
!define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\SCrip"
!macro TestTrace MESSAGE
  !ifdef TEST_INSTALL_DIR
    FileOpen $9 "${TEST_INSTALL_DIR}.trace" a
    FileWrite $9 "${MESSAGE}|$INSTDIR$\r$\n"
    FileClose $9
  !endif
!macroend

Name "SCrip"
OutFile "${OUTPUT}"
!ifdef TEST_INSTALL_DIR
InstallDir "${TEST_INSTALL_DIR}"
!else
InstallDir "$LOCALAPPDATA\Programs\SCrip"
!endif
BrandingText "SCrip"
SetCompressor /SOLID lzma
Icon "app.ico"
VIProductVersion "1.4.2.0"
VIAddVersionKey /LANG=1049 "ProductName" "SCrip"
VIAddVersionKey /LANG=1049 "FileDescription" "Установка SCrip"
VIAddVersionKey /LANG=1049 "FileVersion" "1.4.2"
VIAddVersionKey /LANG=1049 "LegalCopyright" "SCrip"
!define MUI_ICON "app.ico"
!define MUI_UNICON "app.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "welcome.bmp"
!define MUI_WELCOMEPAGE_TITLE "Установить SCrip"
!define MUI_WELCOMEPAGE_TEXT "Ваша музыка из SoundCloud на компьютере.$\r$\n$\r$\nПриложение установится для текущего пользователя. Ярлыки появятся в меню «Пуск» и на рабочем столе."
!define MUI_WELCOMEPAGE_BUTTON "Установить"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "SCrip установлен"
!define MUI_FINISHPAGE_TEXT "Откройте приложение и войдите в свой аккаунт SoundCloud."
!define MUI_FINISHPAGE_RUN "$INSTDIR\SCrip.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Открыть SCrip"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Russian"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "Для SCrip требуется 64-разрядная Windows."
    Abort
  ${EndIf}
FunctionEnd

Function un.onInit
  ReadRegStr $INSTDIR HKCU "${UNKEY}" "InstallLocation"
  StrCmp $INSTDIR "" 0 un_init_done
  StrCpy $INSTDIR "$EXEDIR"
  un_init_done:
FunctionEnd

!macro CheckClosed
  check_closed_retry:
  nsExec::ExecToStack /OEM '"$SYSDIR\tasklist.exe" /FI "IMAGENAME eq SCrip.exe" /NH'
  Pop $0
  Pop $1
  StrCmp $0 "0" 0 check_closed_prompt
  StrCpy $2 $1 9
  StrCmp $2 "SCrip.exe" check_closed_prompt check_closed_done
  check_closed_prompt:
  IfSilent check_closed_abort
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Закройте SCrip, затем нажмите «Повторить»." IDRETRY check_closed_retry
  check_closed_abort:
  SetErrorLevel 2
  Abort
  check_closed_done:
!macroend

Var BackupDir
Var StageDir
Var Marker
Var RecoveryReady
Var HadPrevious

Function RecoverInterruptedUpdate
  !insertmacro TestTrace "recover_fn"
  IfFileExists "$Marker" 0 recovery_done
  !insertmacro TestTrace "recover_marker_found"
  FileOpen $4 "$Marker" r
  IfErrors recovery_restore
  FileRead $4 $5
  FileClose $4
  StrCmp $5 "COMMIT$\r$\n" recovery_committed recovery_restore
  recovery_committed:
  RMDir /r "$StageDir"
  RMDir /r "$BackupDir"
  IfFileExists "$BackupDir\*.*" recovery_failed
  Delete "$Marker"
  Goto recovery_done
  recovery_restore:
  ; A prior setup was interrupted after taking its recovery copy.
  IfFileExists "$BackupDir\*.*" 0 recovery_failed
  ClearErrors
  ExecWait '"$WINDIR\Sysnative\robocopy.exe" "$BackupDir" "$INSTDIR" /MIR /COPY:DAT /R:1 /W:1' $0
  IfErrors recovery_failed
  !insertmacro TestTrace "recover_robocopy_started"
  IntCmp $0 8 recovery_failed recovery_recovered recovery_failed
  recovery_recovered:
  RMDir /r "$StageDir"
  RMDir /r "$BackupDir"
  IfFileExists "$BackupDir\*.*" recovery_failed
  Delete "$Marker"
  CreateShortcut "$SMPROGRAMS\SCrip.lnk" "$INSTDIR\SCrip.exe"
  CreateShortcut "$DESKTOP\SCrip.lnk" "$INSTDIR\SCrip.exe"
  WriteRegStr HKCU "${UNKEY}" "DisplayName" "SCrip"
  WriteRegStr HKCU "${UNKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNKEY}" "DisplayVersion" "1.4.2"
  WriteRegStr HKCU "${UNKEY}" "DisplayIcon" "$INSTDIR\SCrip.exe"
  WriteRegStr HKCU "${UNKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe" _?=$INSTDIR'
  WriteRegDWORD HKCU "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNKEY}" "NoRepair" 1
  Goto recovery_done
  recovery_failed:
  !insertmacro TestTrace "recover_failed"
  SetErrorLevel 3
  MessageBox MB_OK|MB_ICONSTOP "Не удалось восстановить предыдущую версию SCrip. Файлы восстановления сохранены в $BackupDir."
  Abort
  recovery_done:
FunctionEnd

Function RestorePrevious
  StrCmp $RecoveryReady 1 0 restore_done
  ClearErrors
  ExecWait '"$WINDIR\Sysnative\robocopy.exe" "$BackupDir" "$INSTDIR" /MIR /COPY:DAT /R:1 /W:1' $0
  !insertmacro TestTrace "rollback_copy_exit=$0"
  IfErrors restore_failed
  IntCmp $0 8 restore_failed restore_cleanup restore_failed
  restore_failed:
  SetErrorLevel 3
  MessageBox MB_OK|MB_ICONSTOP "Не удалось восстановить предыдущую версию. Резервная копия сохранена в $BackupDir."
  Abort
  restore_cleanup:
  RMDir /r "$BackupDir"
  IfFileExists "$BackupDir\*.*" restore_cleanup_pending
  Delete "$Marker"
  RMDir /r "$StageDir"
  StrCpy $RecoveryReady 0
  StrCmp $HadPrevious 1 0 restore_done
  CreateShortcut "$SMPROGRAMS\SCrip.lnk" "$INSTDIR\SCrip.exe"
  CreateShortcut "$DESKTOP\SCrip.lnk" "$INSTDIR\SCrip.exe"
  WriteRegStr HKCU "${UNKEY}" "DisplayName" "SCrip"
  WriteRegStr HKCU "${UNKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNKEY}" "DisplayVersion" "1.4.2"
  WriteRegStr HKCU "${UNKEY}" "DisplayIcon" "$INSTDIR\SCrip.exe"
  WriteRegStr HKCU "${UNKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe" _?=$INSTDIR'
  WriteRegDWORD HKCU "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNKEY}" "NoRepair" 1
  Goto restore_done
  restore_cleanup_pending:
  RMDir /r "$StageDir"
  StrCpy $RecoveryReady 0
  restore_done:
FunctionEnd

Function .onInstFailed
  Call RestorePrevious
FunctionEnd

Section
  !insertmacro CheckClosed
  StrCpy $BackupDir "$INSTDIR.__SCrip_backup"
  StrCpy $StageDir "$INSTDIR.__SCrip_stage"
  StrCpy $Marker "$INSTDIR.__SCrip_marker"
  StrCpy $RecoveryReady 0
  StrCpy $HadPrevious 0
  Call RecoverInterruptedUpdate
  IfFileExists "$Marker" preexisting_backup
  ; Never remove an unmarked sibling directory: it may contain user data.
  IfFileExists "$BackupDir\*.*" preexisting_backup
  CreateDirectory "$BackupDir"
  Goto begin_transaction
  preexisting_backup:
  !insertmacro TestTrace "preexisting_backup"
  SetErrorLevel 2
  Abort
  begin_transaction:
  ; A nonempty stage without a transaction marker is not ours to delete.
  IfFileExists "$StageDir\*.*" preexisting_stage
  CreateDirectory "$StageDir"
  SetOutPath "$StageDir"
  File /r "${PAYLOAD}\*"
  !insertmacro TestTrace "payload_staged"
  IfErrors staging_failed
  ; Keep an exact copy until the new payload and metadata are installed.
  IfFileExists "$INSTDIR\*.*" 0 no_previous_files
  ClearErrors
  ExecWait '"$WINDIR\Sysnative\robocopy.exe" "$INSTDIR" "$BackupDir" /E /COPY:DAT /R:1 /W:1' $0
  !insertmacro TestTrace "backup_copy_exit=$0"
  IfErrors backup_failed
  IntCmp $0 8 backup_failed backup_ready backup_failed
  backup_failed:
  RMDir /r "$StageDir"
  RMDir /r "$BackupDir"
  SetErrorLevel 2
  Abort
  backup_ready:
  FileOpen $4 "$Marker" w
  IfErrors marker_failed
  FileWrite $4 "SCrip update in progress$\r$\n"
  FileClose $4
  StrCpy $RecoveryReady 1
  StrCpy $HadPrevious 1
  Goto remove_previous
  no_previous_files:
  StrCpy $RecoveryReady 1
  remove_previous:
  IfFileExists "$INSTDIR\Uninstall.exe" 0 copy_staged
  ClearErrors
  ExecWait '"$INSTDIR\Uninstall.exe" /S _?=$INSTDIR' $0
  !insertmacro TestTrace "old_uninstall_exit=$0"
  StrCmp $0 0 uninstall_exit_ok old_uninstall_exit_failed
  uninstall_exit_ok:
  IfFileExists "$INSTDIR\SCrip.exe" old_uninstall_file_remains
  Goto uninstall_ok
  old_uninstall_exit_failed:
  !ifdef TEST_INSTALL_DIR
    FileOpen $8 "${TEST_INSTALL_DIR}.failure" w
    FileWrite $8 "uninstaller exit=$0$\r$\n"
    FileClose $8
  !endif
  Goto old_uninstall_restore
  old_uninstall_file_remains:
  !ifdef TEST_INSTALL_DIR
    FileOpen $8 "${TEST_INSTALL_DIR}.failure" w
    FileWrite $8 "uninstaller left SCrip.exe; result=$0$\r$\n"
    FileClose $8
  !endif
  old_uninstall_restore:
  Call RestorePrevious
  SetErrorLevel 2
  Abort
  uninstall_ok:
  !ifdef TEST_PAUSE_AFTER_UNINSTALL
    IfFileExists "$INSTDIR.__SCrip_test_pause" 0 test_pause_done
    !insertmacro TestTrace "test_pause_entered"
    test_pause_wait:
    IfFileExists "$INSTDIR.__SCrip_test_pause" 0 test_pause_done
    Sleep 100
    Goto test_pause_wait
    test_pause_done:
  !endif
  Goto copy_staged
  copy_staged:
  ClearErrors
  ExecWait '"$WINDIR\Sysnative\robocopy.exe" "$StageDir" "$INSTDIR" /E /COPY:DAT /R:1 /W:1' $0
  !insertmacro TestTrace "payload_copy_exit=$0"
  IfErrors new_copy_failed
  !insertmacro TestTrace "payload_copy_exit=$0"
  IntCmp $0 8 new_copy_failed write_new_uninstaller new_copy_failed
  new_copy_failed:
  !insertmacro TestTrace "new_copy_failed"
  Call RestorePrevious
  SetErrorLevel 2
  Abort
  write_new_uninstaller:
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  IfErrors new_copy_failed
  FileOpen $4 "$Marker" w
  IfErrors new_copy_failed
  FileWrite $4 "COMMIT$\r$\n"
  FileClose $4
  RMDir /r "$StageDir"
  RMDir /r "$BackupDir"
  IfFileExists "$BackupDir\*.*" commit_cleanup_pending
  Delete "$Marker"
  commit_cleanup_pending:
  StrCpy $RecoveryReady 0
  Goto install_shortcuts
  marker_failed:
  !insertmacro TestTrace "marker_failed"
  RMDir /r "$StageDir"
  RMDir /r "$BackupDir"
  SetErrorLevel 2
  Abort
  staging_failed:
  RMDir /r "$StageDir"
  SetErrorLevel 2
  Abort
  preexisting_stage:
  !insertmacro TestTrace "preexisting_stage"
  SetErrorLevel 2
  Abort
  install_shortcuts:
  SetOutPath "$INSTDIR"
  CreateShortcut "$SMPROGRAMS\SCrip.lnk" "$INSTDIR\SCrip.exe"
  CreateShortcut "$DESKTOP\SCrip.lnk" "$INSTDIR\SCrip.exe"
  WriteRegStr HKCU "${UNKEY}" "DisplayName" "SCrip"
  WriteRegStr HKCU "${UNKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNKEY}" "DisplayVersion" "1.4.2"
  WriteRegStr HKCU "${UNKEY}" "DisplayIcon" "$INSTDIR\SCrip.exe"
  WriteRegStr HKCU "${UNKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe" _?=$INSTDIR'
  WriteRegDWORD HKCU "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  !insertmacro CheckClosed
  !include "${FILELIST}"
  Delete "$DESKTOP\SCrip.lnk"
  Delete "$SMPROGRAMS\SCrip.lnk"
  DeleteRegKey HKCU "${UNKEY}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd

Function .onInstSuccess
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/AUTORESTART" $1
  IfErrors done_restart
  Exec '"$INSTDIR\SCrip.exe"'
  done_restart:
FunctionEnd

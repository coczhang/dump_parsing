@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
powershell.exe -ExecutionPolicy Bypass -File "%SCRIPT_DIR%resolve_mingw_crash_address.ps1" %*
exit /b %ERRORLEVEL%

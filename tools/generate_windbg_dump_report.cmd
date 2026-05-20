@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
powershell.exe -ExecutionPolicy Bypass -File "%SCRIPT_DIR%generate_windbg_dump_report.ps1" %*
exit /b %ERRORLEVEL%

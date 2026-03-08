@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%deploy_latest_download_v3.ps1" -MaxAgeMinutes 60
echo.
echo Fenster schliesst automatisch in 60 Sekunden...
timeout /t 60 /nobreak >nul

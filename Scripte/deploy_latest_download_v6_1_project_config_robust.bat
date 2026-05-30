@echo off
setlocal EnableExtensions

rem ============================================================
rem Startet das generische Deploy-Skript mit build_projects.json.
rem Erwartete Ablage:
rem   C:\GitHub\releases\Scripte\deploy_latest_download_v6_1_project_config_robust.ps1
rem   C:\GitHub\releases\Scripte\build_projects.json
rem ============================================================

set "SCRIPT_DIR=%~dp0"
set "PS_SCRIPT=%SCRIPT_DIR%deploy_latest_download_v6_1_project_config_robust.ps1"
set "PROJECT_CONFIG=%SCRIPT_DIR%build_projects.json"

if not exist "%PS_SCRIPT%" (
    echo FEHLER: PowerShell-Skript nicht gefunden:
    echo %PS_SCRIPT%
    echo.
    echo Lege deploy_latest_download_v6_1_project_config_robust.ps1 in denselben Ordner wie diese BAT.
    goto end_error
)

if not exist "%PROJECT_CONFIG%" (
    echo FEHLER: Projekt-Konfiguration nicht gefunden:
    echo %PROJECT_CONFIG%
    echo.
    echo Lege build_projects.json in denselben Ordner wie diese BAT.
    goto end_error
)

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
  -File "%PS_SCRIPT%" ^
  -ProjectConfigPath "%PROJECT_CONFIG%" ^
  -BuildMode ask ^
  -BuildDefault yes ^
  -PromptTimeoutSeconds 60 ^
  -MaxAgeMinutes 60

set "EXIT_CODE=%ERRORLEVEL%"

echo.
if "%EXIT_CODE%"=="0" (
    echo Deploy-Skript wurde beendet. ExitCode: %EXIT_CODE%
) else (
    echo Deploy-Skript wurde mit Fehler beendet. ExitCode: %EXIT_CODE%
)

goto end_wait


:end_error
set "EXIT_CODE=1"


:end_wait
echo.
echo Fenster schliesst automatisch in 60 Sekunden...
timeout /t 60 /nobreak >nul
exit /b %EXIT_CODE%

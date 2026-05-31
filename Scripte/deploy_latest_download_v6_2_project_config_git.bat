@echo off
setlocal EnableExtensions

rem ============================================================
rem Deploy + optionale Kompilierung + Git-Veröffentlichung
rem Erwartete Ablage:
rem   C:\GitHub\releases\Scripte\deploy_latest_download_v6_1_project_config_robust.ps1
rem   C:\GitHub\releases\Scripte\git_publish_from_project_config.ps1
rem   C:\GitHub\releases\Scripte\build_projects.json
rem ============================================================

set "SCRIPT_DIR=%~dp0"
set "DEPLOY_PS=%SCRIPT_DIR%deploy_latest_download_v6_1_project_config_robust.ps1"
set "GIT_PS=%SCRIPT_DIR%git_publish_from_project_config.ps1"
set "PROJECT_CONFIG=%SCRIPT_DIR%build_projects.json"
set "REPO_ROOT=C:\GitHub\releases"

if not exist "%DEPLOY_PS%" (
    echo FEHLER: Deploy-PowerShell-Skript nicht gefunden:
    echo %DEPLOY_PS%
    goto end_error
)

if not exist "%GIT_PS%" (
    echo FEHLER: Git-Publish-Skript nicht gefunden:
    echo %GIT_PS%
    goto end_error
)

if not exist "%PROJECT_CONFIG%" (
    echo FEHLER: Projekt-Konfiguration nicht gefunden:
    echo %PROJECT_CONFIG%
    goto end_error
)

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
  -File "%DEPLOY_PS%" ^
  -ProjectConfigPath "%PROJECT_CONFIG%" ^
  -BuildMode ask ^
  -BuildDefault yes ^
  -PromptTimeoutSeconds 60 ^
  -MaxAgeMinutes 60

set "DEPLOY_EXIT=%ERRORLEVEL%"

echo.
if not "%DEPLOY_EXIT%"=="0" (
    echo Deploy-Skript wurde mit Fehler beendet. ExitCode: %DEPLOY_EXIT%
    echo Git-Veröffentlichung wird nicht gestartet.
    set "EXIT_CODE=%DEPLOY_EXIT%"
    goto end_wait
)

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
  -File "%GIT_PS%" ^
  -RepoRoot "%REPO_ROOT%" ^
  -ProjectConfigPath "%PROJECT_CONFIG%" ^
  -PromptMode ask ^
  -DefaultAnswer yes ^
  -TimeoutSeconds 60

set "GIT_EXIT=%ERRORLEVEL%"

echo.
if "%GIT_EXIT%"=="0" (
    echo Git-Publish-Pruefung wurde beendet. ExitCode: %GIT_EXIT%
) else (
    echo Git-Publish-Pruefung wurde mit Fehler beendet. ExitCode: %GIT_EXIT%
)

set "EXIT_CODE=0"
goto end_wait


:end_error
set "EXIT_CODE=1"


:end_wait
echo.
echo Fenster schliesst automatisch in 60 Sekunden...
timeout /t 60 /nobreak >nul
exit /b %EXIT_CODE%

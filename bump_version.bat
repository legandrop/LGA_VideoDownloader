@echo off
setlocal
rem Bump de version para publicar un release: sube CMakeLists.txt y VERSION a la version que encabeza
rem docs\ChangeLog.md y ofrece commitear, pushear y correr instalador.bat --publish. Lo corre Lega cuando decide
rem publicar, nunca se corre solo. La logica esta en tools\bump_version.ps1; esto es solo el wrapper.
rem Uso: bump_version.bat [--check] [--commit^|--no-commit] [--publish^|--no-publish]
set "FLAGS="
:args
if "%~1"=="" goto run
set "FLAG="
if /I "%~1"=="--check" set "FLAG=-Check"
if /I "%~1"=="--commit" set "FLAG=-Commit"
if /I "%~1"=="--no-commit" set "FLAG=-NoCommit"
if /I "%~1"=="--publish" set "FLAG=-Publish"
if /I "%~1"=="--no-publish" set "FLAG=-NoPublish"
if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help
if not defined FLAG (
    echo [bump_version] ERROR: flag desconocido: %~1
    endlocal & exit /b 2
)
set "FLAGS=%FLAGS% %FLAG%"
shift
goto args
:help
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-Help '%~dp0tools\bump_version.ps1' -Detailed"
endlocal & exit /b 0
:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\bump_version.ps1" %FLAGS%
set "EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %EXIT_CODE%

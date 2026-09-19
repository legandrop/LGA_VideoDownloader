@echo off
setlocal
rem Sincroniza la version del repo.
rem La logica esta en tools\sync_version.py; esto es solo el wrapper de Windows.
set "SCRIPT=%~dp0tools\sync_version.py"
if not exist "%SCRIPT%" (
    echo ERROR: No se encontro "%SCRIPT%".
    exit /b 1
)

rem Orden de busqueda del interprete. Cada candidato se PRUEBA corriendolo:
rem en Windows, "python" y "python3" existen en el PATH como alias del
rem Microsoft Store, imprimen "Python was not found" y salen con error, asi que
rem verificar que estan no alcanza.
set "PY="
set "PYARGS="
call :probar "%~dp0python_runtime\windows\python.exe"
call :probar "%~dp0..\LGA_FileManagerS3\python_runtime\windows\python.exe"
call :probar "py" "-3"
call :probar "python3"
call :probar "python"

if not defined PY (
    echo ERROR: no encontre un interprete de Python usable.
    echo   Se busca, en orden: el runtime del repo, el de ..\LGA_FileManagerS3,
    echo   el launcher "py -3", y "python3"/"python" del PATH.
    exit /b 1
)

"%PY%" %PYARGS% "%SCRIPT%" %*
set "EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %EXIT_CODE%

:probar
if defined PY goto :eof
"%~1" %~2 -c "pass" >nul 2>&1
if errorlevel 1 goto :eof
set "PY=%~1"
set "PYARGS=%~2"
goto :eof

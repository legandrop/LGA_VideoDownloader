@echo off
setlocal
call "%~dp0instalador.bat" --local %*
exit /b %ERRORLEVEL%

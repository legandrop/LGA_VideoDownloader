@echo off
REM setlocal: el cd /d de abajo no se le queda al cmd que llamo a este script.
setlocal
REM Las carpetas se borran relativas a ESTE script, no a la carpeta desde la que se lo llama:
REM antes, corrido desde otra carpeta, borraba el "build" de esa otra carpeta.
cd /d "%~dp0"
echo Limpiando VideoDownloader...

REM Cierra SOLO las copias que corren desde las carpetas que se van a borrar; la app instalada y
REM las de otros checkouts siguen vivas. Ver tools\close_by_path.ps1. Sale con 2 solo si rechazo
REM los parametros: ahi no se borra nada.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\close_by_path.ps1" -ExeName VideoDownloader.exe -Prefix "%~dp0build,%~dp0build-release"
if %ERRORLEVEL% equ 2 ( echo Error: close_by_path rechazo los parametros, no se borra nada & exit /b 1 )

REM build\ es el arbol de desarrollo [Debug] y build-release\ el que empaqueta deploy.bat
REM [Release]. OJO: build\tools tiene el yt-dlp y el deno que la app actualizo sola; despues de
REM limpiar, la app los vuelve a bajar al abrirse.
if exist build (
    echo Eliminando build\...
    rmdir /s /q build
)
if exist build-release (
    echo Eliminando build-release\...
    rmdir /s /q build-release
)

echo.
echo Limpieza completada.
echo.
exit /b 0

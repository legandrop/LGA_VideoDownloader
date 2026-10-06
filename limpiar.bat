@echo off
REM setlocal: el cd /d de abajo no se le queda al cmd que llamo a este script.
setlocal
REM Todo va por la ruta de ESTE script, no por la carpeta desde la que se lo llama: antes,
REM corrido desde otra carpeta, borraba el "build" de esa otra carpeta.
set "APP_ROOT=%~dp0"
cd /d "%APP_ROOT%"
echo Limpiando VideoDownloader...

REM Cierra SOLO lo que corre desde las carpetas que se van a borrar; la app instalada y las de
REM otros checkouts siguen vivas. Ver tools\close_by_path.ps1. Sale con 2 solo si rechazo los
REM parametros: ahi no se borra nada.
REM -Tree: con cada copia de la app se cierran tambien sus hijos que viven dentro de esas
REM carpetas [el yt-dlp, el deno y el ffmpeg de build\tools]; lo que cuelgue de la app y viva
REM en otro lado se deja vivo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%APP_ROOT%tools\close_by_path.ps1" -ExeName VideoDownloader.exe -Prefix "%APP_ROOT%build,%APP_ROOT%build-release" -Tree
if %ERRORLEVEL% equ 2 ( echo Error: close_by_path rechazo los parametros, no se borra nada & exit /b 1 )

REM Las tools que hayan quedado corriendo SIN la app [se cerro mal y el yt-dlp o el ffmpeg
REM siguieron]: -Tree no las alcanza porque ya no cuelgan de nadie. Por nombre Y por carpeta:
REM solo las de las dos carpetas de tools que se van a borrar, nunca las de otra app.
set "CLOSE_REJECTED=0"
for %%T in (yt-dlp.exe deno.exe ffmpeg.exe ffprobe.exe) do call :close_tool %%T
if "%CLOSE_REJECTED%"=="1" ( echo Error: close_by_path rechazo los parametros, no se borra nada & exit /b 1 )

REM build\ es el arbol de desarrollo [Debug] y build-release\ el que empaqueta deploy.bat
REM [Release]. OJO: build\tools tiene el yt-dlp y el deno que la app actualizo sola; despues de
REM limpiar, la app los vuelve a bajar al abrirse.
set "LEFT_BEHIND=0"
call :remove_tree "%APP_ROOT%build"
call :remove_tree "%APP_ROOT%build-release"

echo.
if "%LEFT_BEHIND%"=="1" (
    echo ERROR: la limpieza quedo INCOMPLETA: hay archivos en uso. Cerrar lo que los tenga
    echo        abiertos y volver a correr limpiar.bat.
    exit /b 1
)
echo Limpieza completada.
echo.
exit /b 0

REM ---- Subrutinas. Van aparte porque cmd.exe expande las variables de un bloque entre
REM      parentesis al parsearlo: leer ERRORLEVEL o una bandera adentro daria el valor viejo.

:close_tool
powershell -NoProfile -ExecutionPolicy Bypass -File "%APP_ROOT%tools\close_by_path.ps1" -ExeName %~1 -Prefix "%APP_ROOT%build\tools,%APP_ROOT%build-release\tools"
if %ERRORLEVEL% equ 2 set "CLOSE_REJECTED=1"
goto :eof

:remove_tree
REM Ruta absoluta entre comillas. Un proceso recien cerrado puede tardar un instante en
REM soltar sus archivos: si la carpeta sigue ahi se espera un segundo y se reintenta una vez.
REM Lo que quede despues se lista y la limpieza NO se da por completada.
if not exist "%~1" goto :eof
echo Eliminando %~1 ...
rmdir /s /q "%~1"
if not exist "%~1" goto :eof
ping -n 2 127.0.0.1 >nul
rmdir /s /q "%~1"
if not exist "%~1" goto :eof
echo ERROR: no se pudo borrar entera "%~1". Quedo:
dir /a /b /s "%~1"
set "LEFT_BEHIND=1"
goto :eof

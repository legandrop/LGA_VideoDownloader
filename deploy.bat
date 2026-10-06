@echo off
REM Uso: deploy.bat [--no-run]
REM   --no-run  arma deploy\ pero no abre la app al terminar.
set "NO_RUN="
if /I "%~1"=="--no-run" set "NO_RUN=1"
set "APP_ROOT=%~dp0"
cd /d "%APP_ROOT%"

REM Cerrar SOLO las copias que corren desde deploy\, que se borra entera. Va antes del rmdir,
REM que con el exe abierto dejaba la carpeta a medias. La copia de build-release\ la cierra
REM compilar.bat --no-run [solo esa, por ruta exacta] y la de build\ ya no se toca: el deploy
REM compila en su propio arbol. Antes era un taskkill por nombre que cerraba tambien la app
REM instalada con sus descargas en curso.
powershell -NoProfile -ExecutionPolicy Bypass -File "%APP_ROOT%tools\close_by_path.ps1" -ExeName VideoDownloader.exe -Prefix "%APP_ROOT%deploy"
if %ERRORLEVEL% equ 2 ( echo Error: close_by_path rechazo los parametros & exit /b 1 )

REM Eliminar carpeta deploy si existe para asegurar un entorno limpio
echo Limpiando carpeta de deploy anterior...
if exist deploy rmdir /S /Q deploy

echo Implementando VideoDownloader...

REM Añadir Qt al PATH
set PATH=%PATH%;C:\Qt\6.8.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin

REM Crear directorio de implementación si no existe
if not exist deploy mkdir deploy

REM Compilar en Release con el motor unico, en build-release\: el arbol de desarrollo [build\,
REM Debug] no se toca y lo que se publica nunca sale de ahi. compilar.bat corta si el arbol no
REM quedo configurado en Release.
call "%APP_ROOT%compilar.bat" --release --no-run
if %ERRORLEVEL% neq 0 (
    echo.
    echo Error en la compilacion Release. No se arma el deploy.
    exit /b 1
)
cd /d "%APP_ROOT%"

REM Copiar el ejecutable al directorio de implementación
copy /Y build-release\VideoDownloader.exe deploy\
if %ERRORLEVEL% neq 0 (
    echo Error: no se pudo copiar build-release\VideoDownloader.exe
    exit /b 1
)

REM Usar windeployqt para copiar todas las DLLs de Qt necesarias
C:\Qt\6.8.2\mingw_64\bin\windeployqt.exe --release deploy\VideoDownloader.exe

REM Crear carpeta tools en deploy y copiar herramientas
echo.
echo Preparando carpeta tools para deploy...
if not exist deploy\tools mkdir deploy\tools
if exist tools\*.* (
    echo Copiando herramientas a carpeta deploy...
    REM Solo binarios. `tools\` tambien aloja utilidades del repo que no se
    REM distribuyen con la app, y un `*.*` las empaqueta sin que nadie lo note.
    copy /Y tools\*.exe deploy\tools\
    copy /Y tools\*.dll deploy\tools\
) else (
    echo Carpeta tools no encontrada o vacía.
)

REM Extension de navegador (se carga con Load unpacked desde esta carpeta) y el JSON del host
REM de Native Messaging con "path" relativo al exe. El instalador registra la clave de HKCU.
echo.
echo Copiando la extension de navegador...
if exist deploy\extension rmdir /S /Q deploy\extension
xcopy /E /I /Y /Q extension deploy\extension
if %ERRORLEVEL% neq 0 (
    echo Error al copiar la extension de navegador.
    exit /b 1
)
copy /Y build-release\com.lga.videodownloader.json deploy\
if %ERRORLEVEL% neq 0 (
    echo Error: falta build-release\com.lga.videodownloader.json
    exit /b 1
)

echo.
echo Implementacion completada exitosamente.
echo La aplicacion portable esta en la carpeta 'deploy'.
echo.

REM Ejecutar la aplicación implementada
if defined NO_RUN (
    echo Ejecucion omitida ^(--no-run^).
) else (
    echo Ejecutando VideoDownloader...
    REM La abre el Explorador de Windows y no esta consola: lanzada con start quedaba dentro del
    REM arbol de procesos de la terminal y se cerraba con ella. Ruta absoluta por APP_ROOT, que
    REM se define fuera de este bloque. explorer.exe devuelve siempre 1: no se mira su codigo.
    explorer.exe "%APP_ROOT%deploy\VideoDownloader.exe"
)
exit /b 0

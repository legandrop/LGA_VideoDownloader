@echo off
REM setlocal: ni el PATH ampliado ni el cd de abajo se le quedan al cmd que llamo al script.
setlocal
REM Uso: deploy.bat [--no-run]
REM   --no-run  arma deploy\ pero no abre la app al terminar.
REM La carpeta del script se toma ANTES de parsear los argumentos.
set "APP_ROOT=%~dp0"
set "NO_RUN="
set "BAD_ARG="

:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="--no-run" ( set "NO_RUN=1" & shift /1 & goto :parse_args )
set "BAD_ARG=%~1"

:args_done
REM Una opcion mal escrita corta aca: ignorarla hacia que un --no-run con un error de tipeo
REM armara el deploy y abriera la app.
if defined BAD_ARG goto :bad_arg
cd /d "%APP_ROOT%"

echo Implementando VideoDownloader...

REM Qt y MinGW al PATH: windeployqt busca ahi el runtime del compilador.
set "WINDEPLOYQT=C:\Qt\6.8.2\mingw_64\bin\windeployqt.exe"
set "PATH=%PATH%;C:\Qt\6.8.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin"

REM Compilar en Release con el motor unico, en build-release\: el arbol de desarrollo [build\,
REM Debug] no se toca y lo que se publica nunca sale de ahi. compilar.bat corta si el arbol no
REM quedo configurado en Release. Va ANTES de tocar deploy\: si la compilacion falla, el deploy
REM anterior queda como estaba.
call "%APP_ROOT%compilar.bat" --release --no-run
if %ERRORLEVEL% neq 0 (
    echo.
    echo Error en la compilacion Release. No se arma el deploy: el anterior queda como estaba.
    exit /b 1
)
cd /d "%APP_ROOT%"
if not exist "%WINDEPLOYQT%" (
    echo Error: no se encontro windeployqt en "%WINDEPLOYQT%". No se toca el deploy anterior.
    exit /b 1
)

REM Recien ahora, con el exe Release listo, se reemplaza el deploy anterior.
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
if exist deploy (
    echo Error: no se pudo borrar entera la carpeta deploy [hay archivos en uso]. No se arma
    echo        el deploy encima de restos del anterior.
    exit /b 1
)
mkdir deploy
if %ERRORLEVEL% neq 0 (
    echo Error: no se pudo crear la carpeta deploy.
    exit /b 1
)

REM Copiar el ejecutable al directorio de implementación
copy /Y build-release\VideoDownloader.exe deploy\
if %ERRORLEVEL% neq 0 (
    echo Error: no se pudo copiar build-release\VideoDownloader.exe
    exit /b 1
)

REM Usar windeployqt para copiar todas las DLLs de Qt necesarias
"%WINDEPLOYQT%" --release deploy\VideoDownloader.exe
if %ERRORLEVEL% neq 0 (
    echo.
    echo Error: windeployqt fallo. El deploy quedo incompleto y no sirve para el instalador.
    exit /b 1
)

REM Crear carpeta tools en deploy y copiar herramientas
echo.
echo Preparando carpeta tools para deploy...
if not exist deploy\tools mkdir deploy\tools
echo Copiando herramientas a carpeta deploy...
REM Solo binarios. `tools\` tambien aloja utilidades del repo que no se
REM distribuyen con la app, y un `*.*` las empaqueta sin que nadie lo note.
copy /Y tools\*.exe deploy\tools\
if %ERRORLEVEL% neq 0 (
    echo Error: no se pudieron copiar los .exe de tools\ a deploy\tools.
    exit /b 1
)
copy /Y tools\*.dll deploy\tools\
if %ERRORLEVEL% neq 0 (
    echo Error: no se pudieron copiar las .dll de tools\ a deploy\tools.
    exit /b 1
)
REM ffmpeg no se baja aparte: si no va en el instalador, la app instalada no puede descargar.
if not exist deploy\tools\ffmpeg.exe (
    echo Error: falta deploy\tools\ffmpeg.exe. Revisar la carpeta tools\ del repo.
    exit /b 1
)
if not exist deploy\tools\yt-dlp.exe (
    echo Error: falta deploy\tools\yt-dlp.exe. Revisar la carpeta tools\ del repo.
    exit /b 1
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

:bad_arg
echo ERROR: opcion desconocida: %BAD_ARG%
echo Uso: deploy.bat [--no-run]
exit /b 1

@echo off
REM Uso: compilar.bat [--no-run]
REM   --no-run  compila y prepara build\tools, pero no abre la app al terminar.
set "NO_RUN="
if /I "%~1"=="--no-run" set "NO_RUN=1"
set "APP_ROOT=%~dp0"
cd /d "%APP_ROOT%"

echo Compilando VideoDownloader...

REM Cierre antes de compilar (Windows bloquea el .exe mientras corre y el link fallaria). La
REM decision la toma la ruta real de cada proceso (tools\close_by_path.ps1), nunca el nombre solo.
REM - Sin --no-run (compila y LANZA): la app es de instancia unica, asi que se cierran TODAS las
REM   copias de VideoDownloader.exe (la instalada, la de build, las de otros checkouts) con sus
REM   yt-dlp, deno y ffmpeg, cada uno por la carpeta de su instancia: nunca los de otra app con el
REM   mismo nombre de proceso. %LOCALAPPDATA%\LGA\VideoDownloader es la carpeta de tools de
REM   fallback, compartida por las copias instaladas en una carpeta no escribible. El "." final
REM   evita que la \ de APP_ROOT escape la comilla de cierre. Sin -Tree a proposito: todo lo
REM   que lanza la app ya esta en -Helpers, y con -Tree caeria el instalador del auto-update,
REM   que la app lanza como hijo desde {app}\updates (o desde la carpeta de fallback).
REM - Con --no-run (el modo de toda corrida automatizada): SOLO la copia de build\, el archivo que
REM   el linker necesita libre.
REM Sale con 2 solo si rechazo los parametros: ahi se corta.
if defined NO_RUN (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%APP_ROOT%tools\close_by_path.ps1" -ExeName VideoDownloader.exe -ExactPath "%APP_ROOT%build\VideoDownloader.exe"
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%APP_ROOT%tools\close_by_path.ps1" -ExeName VideoDownloader.exe -AllInstances -Helpers yt-dlp.exe,deno.exe,ffmpeg.exe -HelperPrefix "%APP_ROOT%.,%LOCALAPPDATA%\LGA\VideoDownloader"
)
if %ERRORLEVEL% equ 2 ( echo Error: close_by_path rechazo los parametros & exit /b 1 )

REM Añadir Qt y MinGW al PATH
set PATH=%PATH%;C:\Qt\6.8.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin

REM Crear directorio de compilación si no existe
if not exist build mkdir build
cd build

REM Configurar el proyecto
echo Configurando con CMake...
cmake .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:/Qt/6.8.2/mingw_64"

REM Compilar el proyecto
echo Compilando VideoDownloader...
cmake --build .

REM Verificar si la compilación fue exitosa
if %ERRORLEVEL% neq 0 (
    echo.
    echo Error en la compilación. Verifique los mensajes de error.
    cd ..
    exit /b 1
)

REM Crear carpeta tools si no existe y copiar herramientas
echo.
echo Preparando carpeta tools...
if not exist tools mkdir tools
if exist ..\tools\*.* (
    echo Copiando herramientas desde carpeta tools del proyecto...
    REM Solo binarios, igual que deploy.bat: `tools\` tambien aloja utilidades
    REM del repo que no son parte de la app.
    REM /D copia solo si el del repo es MAS NUEVO: el boton "Update dlp" baja un
    REM yt-dlp.exe mas reciente a build\tools y un copy /Y lo pisaba en cada build.
    xcopy /D /Y /I /Q ..\tools\*.exe tools\
    xcopy /D /Y /I /Q ..\tools\*.dll tools\
) else (
    echo Carpeta tools del proyecto no encontrada o vacía.
)

REM Ejecutar la aplicación
echo.
echo Compilación completada exitosamente.
if defined NO_RUN (
    echo Ejecucion omitida ^(--no-run^).
) else (
    echo Ejecutando VideoDownloader...
    echo.
    start VideoDownloader.exe
)

cd ..

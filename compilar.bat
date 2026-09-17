@echo off
echo Compilando VideoDownloader...

REM Matar el proceso VideoDownloader si está en ejecución
taskkill /F /IM VideoDownloader.exe 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Proceso VideoDownloader terminado.
    timeout /t 1 >nul
) else (
    echo No se encontró el proceso VideoDownloader en ejecución.
)

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
echo Ejecutando VideoDownloader...
echo.
start VideoDownloader.exe

cd ..

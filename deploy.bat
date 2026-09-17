@echo off
REM Eliminar carpeta deploy si existe para asegurar un entorno limpio
echo Limpiando carpeta de deploy anterior...
if exist deploy rmdir /S /Q deploy

echo Implementando VideoDownloader...

REM Matar procesos previos si están en ejecución
taskkill /F /IM VideoDownloader.exe 2>nul

REM Añadir Qt al PATH
set PATH=%PATH%;C:\Qt\6.8.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin

REM Crear directorio de implementación si no existe
if not exist deploy mkdir deploy

REM Compilar el proyecto en modo Release
cd build
echo Configurando con CMake (modo Release)...
cmake .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:/Qt/6.8.2/mingw_64" -DCMAKE_BUILD_TYPE=Release
echo Compilando VideoDownloader (modo Release)...
cmake --build . --config Release

REM Verificar si la compilación fue exitosa
if %ERRORLEVEL% neq 0 (
    echo.
    echo Error en la compilación. Verifique los mensajes de error.
    cd ..
    exit /b 1
)
cd ..

REM Copiar el ejecutable al directorio de implementación
copy /Y build\VideoDownloader.exe deploy\

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
copy /Y build\com.lga.videodownloader.json deploy\
if %ERRORLEVEL% neq 0 (
    echo Error: falta build\com.lga.videodownloader.json
    exit /b 1
)

echo.
echo Implementacion completada exitosamente.
echo La aplicacion portable esta en la carpeta 'deploy'.
echo.

REM Ejecutar la aplicación implementada
echo Ejecutando VideoDownloader...
start deploy\VideoDownloader.exe

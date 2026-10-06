@echo off
setlocal
REM Motor unico de compilacion en Windows: compila, deja las dependencias de runtime al lado
REM del exe, prepara las tools y abre la app. deploy.bat lo llama con --release --no-run.
REM
REM Uso: compilar.bat [--no-run] [--wait] [--release] [--sim-slow] [--parallel N] [--help]
REM
REM Por defecto compila Debug en build\ y abre la app en segundo plano: el script termina
REM enseguida y nunca retiene la terminal.

REM La carpeta del script se toma ANTES de parsear los argumentos.
set "APP_ROOT=%~dp0"
set "APP_EXE_NAME=VideoDownloader.exe"
set "NO_RUN=false"
set "WAIT_FOR_APP=false"
set "SIM_SLOW=false"
set "SHOW_HELP=false"
REM El build de desarrollo [Debug] y el de release viven en arboles SEPARADOS: con uno solo,
REM alternar entre compilar para trabajar y compilar para publicar reconfigura CMake y
REM recompila todo cada vez.
set "BUILD_TYPE=Debug"
set "BUILD_DIR=build"
set "PARALLEL_CORES=%NUMBER_OF_PROCESSORS%"
set "BAD_ARG="

:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="--no-run"   ( set "NO_RUN=true" & shift /1 & goto :parse_args )
if /I "%~1"=="--wait"     ( set "WAIT_FOR_APP=true" & shift /1 & goto :parse_args )
if /I "%~1"=="--release"  ( set "BUILD_TYPE=Release" & set "BUILD_DIR=build-release" & shift /1 & goto :parse_args )
if /I "%~1"=="--sim-slow" ( set "SIM_SLOW=true" & shift /1 & goto :parse_args )
if /I "%~1"=="--parallel" ( set "PARALLEL_CORES=%~2" & shift /1 & shift /1 & goto :parse_args )
if /I "%~1"=="--help"     ( set "SHOW_HELP=true" & shift /1 & goto :parse_args )
if /I "%~1"=="-h"         ( set "SHOW_HELP=true" & shift /1 & goto :parse_args )
set "BAD_ARG=%~1"

:args_done
REM Una opcion mal escrita corta aca: ignorarla haria que un --no-run con un error de tipeo
REM compilara, cerrara todas las instancias de la app y la abriera.
if defined BAD_ARG goto :bad_arg
if "%SHOW_HELP%"=="true" goto :show_help
if "%PARALLEL_CORES%"=="" goto :bad_parallel
goto :main

:bad_arg
echo ERROR: opcion desconocida: %BAD_ARG%
echo Ver compilar.bat --help
exit /b 1

:bad_parallel
echo ERROR: --parallel necesita un numero de nucleos.
exit /b 1

:show_help
echo Uso: compilar.bat [opciones]
echo.
echo   --no-run      Compila y deja todo listo, sin abrir la app. Cierra solo la copia que
echo                 va a pisar.
echo   --wait        Deja la app en primer plano y devuelve su codigo de salida.
echo   --release     Compila Release en build-release\ [lo que empaqueta deploy.bat].
echo                 Con --release --no-run no se copian las tools.
echo   --sim-slow    Abre la app con prioridad baja y 2 nucleos [simula una maquina lenta].
echo   --parallel N  Nucleos para compilar. Por defecto, todos.
echo.
echo Sin opciones: compila Debug en build\, cierra todas las instancias de la app y la abre.
exit /b 0

:main
cd /d "%APP_ROOT%"

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
REM - Con --no-run: SOLO la copia del arbol que se
REM   compila, el archivo que el linker necesita libre.
REM Sale con 2 solo si rechazo los parametros: ahi se corta.
if "%NO_RUN%"=="true" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%APP_ROOT%tools\close_by_path.ps1" -ExeName VideoDownloader.exe -ExactPath "%APP_ROOT%%BUILD_DIR%\VideoDownloader.exe"
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%APP_ROOT%tools\close_by_path.ps1" -ExeName VideoDownloader.exe -AllInstances -Helpers yt-dlp.exe,deno.exe,ffmpeg.exe -HelperPrefix "%APP_ROOT%.,%LOCALAPPDATA%\LGA\VideoDownloader"
)
if %ERRORLEVEL% equ 2 ( echo Error: close_by_path rechazo los parametros & exit /b 1 )

REM Toolchain, en un solo lugar. QT_DIR_CMAKE es la misma ruta con barras para CMake.
set "QT_DIR=C:\Qt\6.8.2\mingw_64"
set "QT_DIR_CMAKE=C:/Qt/6.8.2/mingw_64"
set "MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin"
set "NINJA_DIR=C:\Qt\Tools\Ninja"

if not exist "%QT_DIR%\bin\Qt6Core.dll" (
    echo ERROR: no se encontro Qt 6.8.2 mingw_64 en "%QT_DIR%".
    exit /b 1
)
if not exist "%MINGW_BIN%\g++.exe" (
    echo ERROR: no se encontro MinGW en "%MINGW_BIN%".
    exit /b 1
)
set "PATH=%PATH%;%QT_DIR%\bin;%MINGW_BIN%;%NINJA_DIR%"
where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake no esta en el PATH.
    exit /b 1
)

echo Compilando VideoDownloader en %BUILD_TYPE% [arbol %BUILD_DIR%\, %PARALLEL_CORES% nucleos]

REM Generador. Un arbol YA configurado conserva el suyo: pasarle otro con -G hace que CMake
REM aborte ["generator does not match"] y la salida seria borrar el arbol, que en build\ se
REM lleva las tools que la app actualizo sola. Un arbol nuevo nace con Ninja; sin Ninja, con
REM MinGW Makefiles. La lectura va en una subrutina: ver la nota de las subrutinas, al final.
set "CACHED_GENERATOR="
if exist "%BUILD_DIR%\CMakeCache.txt" call :read_generator
set "NEW_GENERATOR=MinGW Makefiles"
if exist "%NINJA_DIR%\ninja.exe" set "NEW_GENERATOR=Ninja"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%APP_ROOT%%BUILD_DIR%"

REM Se configura SIEMPRE y con el tipo de build explicito: asi un arbol que quedo con otro tipo
REM en la cache no compila ese otro tipo en silencio. cmake va fuera de bloques para leer su
REM ERRORLEVEL en la linea siguiente.
if defined CACHED_GENERATOR goto :configure_existing
echo Configurando CMake [arbol nuevo, generador %NEW_GENERATOR%]...
cmake .. -G "%NEW_GENERATOR%" -DCMAKE_PREFIX_PATH="%QT_DIR_CMAKE%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if %ERRORLEVEL% neq 0 goto :cmake_failed
goto :build

:configure_existing
echo Configurando CMake [generador del arbol: %CACHED_GENERATOR%]...
cmake .. -DCMAKE_PREFIX_PATH="%QT_DIR_CMAKE%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if %ERRORLEVEL% neq 0 goto :cmake_failed

:build
REM El tipo que quedo en la cache tiene que ser el pedido: publicar un Debug no lo delata nada.
findstr /B /C:"CMAKE_BUILD_TYPE:" CMakeCache.txt | findstr /E /C:"=%BUILD_TYPE%" >nul
if %ERRORLEVEL% neq 0 goto :build_type_mismatch

REM --parallel explicito: con MinGW Makefiles el default es un solo nucleo.
cmake --build . --parallel %PARALLEL_CORES%
if %ERRORLEVEL% neq 0 goto :build_failed
cd /d "%APP_ROOT%"

if not exist "%BUILD_DIR%\%APP_EXE_NAME%" (
    echo ERROR: la compilacion no genero %BUILD_DIR%\%APP_EXE_NAME%.
    exit /b 1
)

REM ============================================================
REM  DEPENDENCIAS DE RUNTIME: verificar - copiar - verificar - windeployqt - verificar
REM
REM  Van AL LADO del exe: la app tiene que arrancar sin el PATH de este script, que es como
REM  la abre explorer.exe mas abajo y como corre instalada. El arbol es incremental, asi que
REM  en la corrida normal esto son N chequeos "if not exist" y no se copia nada.
REM
REM  La lista sale de lo que la app linkea [find_package Core Gui Widgets Network, y las DLL
REM  que importa el exe] y de los plugins que esos modulos cargan: plataforma [qwindows, y
REM  qoffscreen para las capturas y pruebas sin pantalla], estilo, TLS [las descargas de
REM  yt-dlp, deno y el update de la app son HTTPS: sin el backend no hay red] e informacion
REM  de red. Con las DLL de Qt al lado del exe, Qt busca los plugins ahi y ya no en la
REM  instalacion de Qt: lo que no este en esta lista, la app no lo tiene.
REM ============================================================
set "DEP_LIST=libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Network.dll platforms\qwindows.dll platforms\qoffscreen.dll styles\qmodernwindowsstyle.dll tls\qschannelbackend.dll tls\qcertonlybackend.dll networkinformation\qnetworklistmanager.dll"

echo Verificando dependencias de runtime...
set "DEPS_MISSING=false"
for %%D in (%DEP_LIST%) do call :check_dep "%%D"
if "%DEPS_MISSING%"=="true" call :copy_missing_deps
if "%DEPS_MISSING%"=="true" call :recheck_deps

REM Ultimo recurso: windeployqt, acotado para no dejar traducciones ni OpenGL por software.
if "%DEPS_MISSING%"=="true" call :run_windeployqt
if "%DEPS_MISSING%"=="true" call :recheck_deps

if "%DEPS_MISSING%"=="true" (
    echo.
    echo ERROR: faltan dependencias de runtime y no se pudieron reparar.
    echo        Verificar que Qt 6.8.2 mingw_64 este instalado en "%QT_DIR%".
    exit /b 1
)
echo Dependencias de runtime verificadas.

REM ---- tools [yt-dlp, ffmpeg y sus DLL] ----
REM Con --release --no-run no se copian: ese arbol lo empaqueta deploy.bat, que toma las
REM tools del repo. Son 170 MB que ahi nadie usa.
if /I "%BUILD_TYPE%"=="Release" if "%NO_RUN%"=="true" goto :after_tools
echo Preparando %BUILD_DIR%\tools...
if not exist "%BUILD_DIR%\tools" mkdir "%BUILD_DIR%\tools"
REM Solo binarios, igual que deploy.bat: tools\ tambien aloja utilidades del repo que no
REM son parte de la app.
REM /D copia solo si el del repo es MAS NUEVO: la app actualiza sola yt-dlp y deno en
REM build\tools, y un copy /Y los pisaba con los del repo en cada compilacion.
xcopy /D /Y /I /Q tools\*.exe %BUILD_DIR%\tools\
if errorlevel 1 echo AVISO: no se pudieron copiar los .exe de tools\
xcopy /D /Y /I /Q tools\*.dll %BUILD_DIR%\tools\
if errorlevel 1 echo AVISO: no se pudieron copiar las .dll de tools\
:after_tools

echo.
echo Compilacion completada.
if "%NO_RUN%"=="true" (
    echo Ejecucion omitida ^(--no-run^).
    endlocal
    exit /b 0
)

REM ============================================================
REM  LANZAMIENTO
REM ============================================================
set "APP_EXE=%APP_ROOT%%BUILD_DIR%\%APP_EXE_NAME%"

REM --sim-slow: /LOW baja la prioridad a idle [en Windows tambien baja la de I/O] y
REM /AFFINITY 3 [mascara 0x3] deja al proceso en 2 nucleos. yt-dlp, ffmpeg y deno heredan las
REM dos cosas. Windows no permite frenar el disco por linea de comandos, asi que es mas suave
REM que la simulacion de macOS: comparar siempre contra otra corrida de Windows.
set "START_OPTS="
if "%SIM_SLOW%"=="true" set "START_OPTS=/LOW /AFFINITY 3"
if "%SIM_SLOW%"=="true" echo [sim-slow] Prioridad baja + 2 nucleos ^(simulacion de maquina lenta^)

cd /d "%APP_ROOT%%BUILD_DIR%"
if "%WAIT_FOR_APP%"=="true" goto :launch_wait
if "%SIM_SLOW%"=="true" goto :launch_sim_slow

REM La abre el Explorador de Windows, no esta consola. Lanzada con start desde una terminal, la
REM app queda dentro del arbol de procesos de esa terminal y se cierra con ella. explorer.exe
REM la arranca fuera de ese arbol: nace con cwd System32, sin el entorno de este script [por
REM eso las DLL van al lado del exe] y sin argumentos. La ruta va absoluta y por variables
REM definidas fuera de bloques, nunca por CD ni relativa. explorer.exe devuelve siempre 1.
echo Abriendo VideoDownloader...
explorer.exe "%APP_EXE%"
endlocal
exit /b 0

:launch_sim_slow
REM Necesita las opciones de start, que explorer.exe no acepta: esta corrida de diagnostico SI
REM queda colgada de la consola que la lanzo. El .\ no es cosmetico: con
REM NoDefaultCurrentDirectoryInExePath cmd.exe no busca ejecutables en el directorio actual.
echo Abriendo VideoDownloader [sim-slow]...
start "" %START_OPTS% .\%APP_EXE_NAME%
endlocal
exit /b 0

:launch_wait
echo === INICIO DE EJECUCION [--wait] ===
start "" /WAIT %START_OPTS% .\%APP_EXE_NAME%
REM Fuera de un bloque: aca ERRORLEVEL se expande al ejecutar esta linea, despues de la app.
set "APP_EXIT_CODE=%ERRORLEVEL%"
echo === FIN DE EJECUCION - codigo de salida: %APP_EXIT_CODE% ===
if "%APP_EXIT_CODE%"=="-1073741819" echo DIAGNOSTICO: acceso a memoria invalido
if "%APP_EXIT_CODE%"=="-1073741571" echo DIAGNOSTICO: desbordamiento del stack
if "%APP_EXIT_CODE%"=="-1073741515" echo DIAGNOSTICO: falta una DLL al lado del exe
endlocal & exit /b %APP_EXIT_CODE%

:cmake_failed
echo.
echo ERROR: fallo la configuracion de CMake.
exit /b 1

:build_type_mismatch
echo.
echo ERROR: el arbol %BUILD_DIR%\ no quedo configurado en %BUILD_TYPE%. No se compila.
exit /b 1

:build_failed
echo.
echo ERROR: fallo la compilacion. Ver los mensajes de arriba.
exit /b 1

REM ============================================================
REM  Subrutinas
REM
REM  Van en subrutinas y no en linea a proposito: cmd.exe expande las variables de un bloque
REM  entre parentesis al PARSEARLO, no al ejecutarlo, asi que una variable que se escribe y se
REM  lee dentro del mismo bloque lee siempre el valor viejo. Un call reparsea el cuerpo en
REM  cada invocacion.
REM ============================================================

:read_generator
for /f "tokens=2 delims==" %%A in ('findstr /C:"CMAKE_GENERATOR:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt"') do set "CACHED_GENERATOR=%%A"
goto :eof

:check_dep
if not exist "%BUILD_DIR%\%~1" (
    echo    [falta] %BUILD_DIR%\%~1
    set "DEPS_MISSING=true"
)
goto :eof

:recheck_deps
set "DEPS_MISSING=false"
for %%D in (%DEP_LIST%) do call :check_dep "%%D"
goto :eof

:copy_missing_deps
echo.
echo Faltan dependencias de runtime. Copiandolas...
REM Sin redirigir los errores a nul: una copia que falla tiene que verse.
call :copy_dep "%MINGW_BIN%" "" libgcc_s_seh-1.dll
call :copy_dep "%MINGW_BIN%" "" libstdc++-6.dll
call :copy_dep "%MINGW_BIN%" "" libwinpthread-1.dll
call :copy_dep "%QT_DIR%\bin" "" Qt6Core.dll
call :copy_dep "%QT_DIR%\bin" "" Qt6Gui.dll
call :copy_dep "%QT_DIR%\bin" "" Qt6Widgets.dll
call :copy_dep "%QT_DIR%\bin" "" Qt6Network.dll
call :copy_dep "%QT_DIR%\plugins\platforms" "platforms" qwindows.dll
call :copy_dep "%QT_DIR%\plugins\platforms" "platforms" qoffscreen.dll
call :copy_dep "%QT_DIR%\plugins\styles" "styles" qmodernwindowsstyle.dll
call :copy_dep "%QT_DIR%\plugins\tls" "tls" qschannelbackend.dll
call :copy_dep "%QT_DIR%\plugins\tls" "tls" qcertonlybackend.dll
call :copy_dep "%QT_DIR%\plugins\networkinformation" "networkinformation" qnetworklistmanager.dll
goto :eof

:run_windeployqt
if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo AVISO: no se encontro windeployqt.exe en "%QT_DIR%\bin".
    goto :eof
)
echo Todavia faltan dependencias: probando con windeployqt...
"%QT_DIR%\bin\windeployqt.exe" --compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --dir "%BUILD_DIR%" "%BUILD_DIR%\%APP_EXE_NAME%"
if errorlevel 1 echo AVISO: windeployqt devolvio error.
goto :eof

:copy_dep
set "DEP_DEST=%BUILD_DIR%"
if not "%~2"=="" set "DEP_DEST=%BUILD_DIR%\%~2"
if exist "%DEP_DEST%\%~3" goto :eof
if not exist "%DEP_DEST%" mkdir "%DEP_DEST%"
if not exist "%~1\%~3" (
    echo    ERROR: no existe el origen "%~1\%~3"
    goto :eof
)
copy /Y "%~1\%~3" "%DEP_DEST%\" >nul
if errorlevel 1 (
    echo    ERROR: fallo la copia de "%~1\%~3"
) else (
    echo    [ok] %~3
)
goto :eof

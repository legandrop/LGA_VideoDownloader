@echo off
setlocal
REM Uso: instalador.bat [--no-run] [--publish o --no-publish]
REM   --no-run      arma el instalador y su SHA256SUMS, y no pregunta si ejecutarlo.
REM   --publish     publica sin preguntar el release v(version) en legandrop/LGA_VideoDownloader:
REM                 el instalador, SHA256SUMS y las notas para el usuario (What's new).
REM   --no-publish  no publica ni pregunta.
REM   --replace     con --publish, reemplaza el instalador de Windows de un release que ya lo tiene.
REM Sin flags de publicacion: antes de armar nada mira el release en GitHub y pregunta con una
REM tecla: publicar (o sumar el instalador al release), instalador local o cancelar.
set "NO_RUN="
set "PUBLISH=prompt"
set "REPLACE="
set "SCRIPT_DIR=%~dp0"
:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="--no-run" ( set "NO_RUN=1" & shift & goto :parse_args )
if /I "%~1"=="--publish" ( set "PUBLISH=always" & shift & goto :parse_args )
if /I "%~1"=="--no-publish" ( set "PUBLISH=never" & shift & goto :parse_args )
if /I "%~1"=="--replace" ( set "REPLACE=1" & shift & goto :parse_args )
echo Error: opcion desconocida: %~1
exit /b 1
:args_done
REM Todas las rutas de aca abajo son relativas a la carpeta del script, no a la carpeta desde la
REM que se lo llama. El setlocal de arriba devuelve la carpeta original al terminar.
cd /d "%SCRIPT_DIR%"

echo Preparando instalador para VideoDownloader...

REM Verificar que deploy tenga el ejecutable actual.
REM No alcanza con que la carpeta exista: un deploy anterior a un rename deja
REM un .exe con el nombre viejo, el instalador lo empaqueta igual y los accesos
REM directos quedan apuntando a un archivo que no existe.
if not exist deploy\VideoDownloader.exe (
    echo Error: falta deploy\VideoDownloader.exe. Ejecute primero deploy.bat
    exit /b 1
)
REM La clave de registro del host apunta a este JSON: sin el, la extension no encuentra la app.
if not exist deploy\com.lga.videodownloader.json (
    echo Error: falta deploy\com.lga.videodownloader.json. Ejecute primero deploy.bat
    exit /b 1
)
if not exist deploy\extension\manifest.json (
    echo Error: falta deploy\extension. Ejecute primero deploy.bat
    exit /b 1
)

REM Abortar si el build es posterior a lo desplegado: `xcopy /L /D` lista el
REM origen solo cuando es mas nuevo que el destino, y no copia nada. Se compara contra
REM build-release\, el arbol del que deploy.bat saca el exe; build\ es el de desarrollo y es
REM normal que sea mas nuevo. find con su ruta: desde Git Bash el find de GNU va primero en el PATH,
REM no entiende /i y el chequeo no veia nunca un deploy viejo.
set "DEPLOY_STALE="
if exist build-release\VideoDownloader.exe (
    for /f "delims=" %%A in ('xcopy build-release\VideoDownloader.exe deploy\ /L /D /Y 2^>nul ^| %SystemRoot%\System32\find.exe /i "VideoDownloader.exe"') do set "DEPLOY_STALE=1"
)
if defined DEPLOY_STALE (
    echo Error: deploy\VideoDownloader.exe es mas viejo que build-release\VideoDownloader.exe
    echo Ejecute deploy.bat para regenerar el deploy antes de armar el instalador.
    exit /b 1
)

REM La version sale de VERSION, el espejo derivado del project() de CMakeLists.txt
REM (lo escribe sync_version). Define el AppVersion y el nombre del instalador, que
REM es lo que busca el auto-update de la app: VideoDownloader_Setup_v<version>.exe.
set "APP_VERSION="
set /p APP_VERSION=<VERSION
if not defined APP_VERSION (
    echo Error: no se pudo leer la version del archivo VERSION
    exit /b 1
)

REM ---- Publicacion: se decide ACA, antes de armar el instalador ----
REM El release vive en este mismo repo. Descubrir al final un gh sin login o notas que faltan
REM es tirar la corrida. Con --publish, lo que impide publicar corta antes de armar nada.
set "RELEASE_REPO=legandrop/LGA_VideoDownloader"
set "INTERACTIVE="
powershell -NoProfile -Command "if ([Console]::IsInputRedirected) { exit 3 } else { exit 0 }"
if not errorlevel 1 set "INTERACTIVE=1"
if /I "%PUBLISH%"=="never" goto :publish_preflight_done
if /I "%PUBLISH%"=="prompt" goto :publish_menu
call :gh_detect
if errorlevel 1 (
    echo gh no esta disponible o no tiene login.
    goto :publish_preflight_failed
)
call :git_preflight
if errorlevel 1 goto :publish_preflight_failed
call :notes_check
if errorlevel 1 goto :publish_preflight_failed
call :release_check
if errorlevel 1 goto :publish_preflight_failed
goto :publish_preflight_done
:publish_preflight_failed
echo ERROR: no se puede publicar; no se armo nada.
exit /b 1

:publish_menu
REM Sin flags se mira GitHub y se pregunta UNA vez, con una tecla y sin Enter [choice], con el
REM criterio de publish_release.sh --decide de la Mac:
REM   1  publicar v(version) como release nuevo, o sumar el instalador al release que ya existe
REM      [la Mac pudo publicar primero]. No se ofrece si el release ya tiene el instalador de
REM      Windows [republicar el mismo numero deja sin update a quien ya lo tiene; para eso esta
REM      --publish --replace] ni si el changelog va adelante: falta el bump, y esta app no tiene
REM      script de bump.
REM   2  instalador local, sin subir nada.     3  cancelar.
REM Si algo impide publicar se dice por que y quedan solo 2 y 3. Una tecla que no es opcion no hace
REM nada; sin entrada [EOF] choice cancela. Una corrida desatendida pasa --publish o --no-publish.
set "MENU_ACTION="
set "MENU_STATUS="
set "MENU_NOTE="
set "PUB_BLOCKER="
set "CL_VERSION="
set "CL_AHEAD="
for /f "tokens=1 delims=:" %%L in ('findstr /r /b /c:"v[0-9][0-9.]*[ ]*:" "docs\ChangeLog.md"') do if not defined CL_VERSION set "CL_VERSION=%%L"
if not defined CL_VERSION goto :publish_menu_gh
set "CL_VERSION=%CL_VERSION:v=%"
set "CL_VERSION=%CL_VERSION: =%"
powershell -NoProfile -Command "try { if ([version]$env:CL_VERSION -gt [version]$env:APP_VERSION.Trim()) { exit 0 } } catch {}; exit 1"
if not errorlevel 1 set "CL_AHEAD=1"
:publish_menu_gh
call :gh_detect
if errorlevel 1 (
    set "PUB_BLOCKER=gh no esta disponible o no tiene login"
    goto :publish_menu_show
)
call :git_preflight
if errorlevel 1 goto :publish_menu_show
call :release_state
if errorlevel 1 goto :publish_menu_show
set "REL_STATE=none"
if defined REL_EXISTS set "REL_STATE=other"
if "%REL_OWN%"=="1" set "REL_STATE=mine"
if "%REL_DRAFT%"=="1" set "PUB_BLOCKER=el release %TAG% existe como borrador; publicarlo o borrarlo a mano"
if "%REL_OTHER%"=="1" if "%REL_SUMS%"=="0" set "PUB_BLOCKER=el release %TAG% tiene los archivos de macOS sin SHA256SUMS; subir primero el de la Mac"
if "%REL_STATE%"=="none" set "MENU_STATUS=v%APP_VERSION% no esta publicada en GitHub."
if "%REL_STATE%"=="none" set "MENU_ACTION=Publicar v%APP_VERSION% como release nuevo"
if "%REL_STATE%"=="other" set "MENU_STATUS=v%APP_VERSION% esta en GitHub, sin el instalador de Windows."
if "%REL_STATE%"=="other" set "MENU_ACTION=Sumar el instalador de Windows al release v%APP_VERSION%"
if "%REL_STATE%"=="mine" set "MENU_STATUS=v%APP_VERSION% ya esta publicada en GitHub con el instalador de Windows."
if "%REL_STATE%"=="mine" set "MENU_NOTE=Para publicar, primero la version nueva en el changelog."
:publish_menu_show
if defined CL_AHEAD set "MENU_ACTION="
if defined CL_AHEAD set "MENU_NOTE=Para publicar v%CL_VERSION% falta el bump [en esta app no esta automatizado]."
if defined PUB_BLOCKER set "MENU_ACTION="
echo.
echo ============================================================
echo  Publicacion en GitHub [VideoDownloader]
echo ============================================================
if defined MENU_STATUS echo %MENU_STATUS%
if defined CL_AHEAD echo El changelog tiene v%CL_VERSION% en desarrollo.
if defined MENU_NOTE echo %MENU_NOTE%
if defined PUB_BLOCKER echo No se puede publicar: %PUB_BLOCKER%.
echo.
if not defined MENU_ACTION goto :publish_menu_fallback
echo   1. %MENU_ACTION%
echo   2. Instalador local, sin subir a GitHub
echo   3. Cancelar
choice /C 123 /N /M "Opcion: "
set "MENU_KEY=%ERRORLEVEL%"
if "%MENU_KEY%"=="2" goto :publish_menu_local
if not "%MENU_KEY%"=="1" goto :publish_menu_cancel
call :notes_check
if errorlevel 1 goto :publish_menu_fallback
set "PUBLISH=always"
echo Se publica v%APP_VERSION% al terminar.
goto :publish_preflight_done
:publish_menu_fallback
echo   2. Instalador local, sin subir a GitHub
echo   3. Cancelar
choice /C 23 /N /M "Opcion: "
if "%ERRORLEVEL%"=="1" goto :publish_menu_local
:publish_menu_cancel
echo Cancelado. No se armo nada.
exit /b 1
:publish_menu_local
set "PUBLISH=never"
echo Se arma solo el instalador local, sin publicar.
:publish_preflight_done

REM Verificar si Inno Setup está instalado
set "INNO_PATH=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%INNO_PATH%" (
    echo Inno Setup no encontrado. Descargando...
    
    REM Crear directorio temporal
    mkdir temp_inno
    cd temp_inno
    
    REM Descargar Inno Setup
    powershell -Command "& {Invoke-WebRequest -Uri 'https://jrsoftware.org/download.php/is.exe' -OutFile 'innosetup.exe'}"
    
    REM Instalar Inno Setup silenciosamente
    echo Instalando Inno Setup...
    start /wait innosetup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
    
    cd ..
    rmdir /S /Q temp_inno
)

REM Crear el script de Inno Setup
echo Generando script de instalador...
echo ; ARCHIVO GENERADO por instalador.bat - no editar a mano, se pisa en cada corrida. > VideoDownloader_installer.iss
echo [Setup] >> VideoDownloader_installer.iss
echo AppId=VideoDownloader >> VideoDownloader_installer.iss
echo AppName=VideoDownloader >> VideoDownloader_installer.iss
echo AppVersion=%APP_VERSION% >> VideoDownloader_installer.iss
echo DefaultDirName=C:\Portable\LGA\VideoDownloader >> VideoDownloader_installer.iss
echo DefaultGroupName=VideoDownloader >> VideoDownloader_installer.iss
echo UninstallDisplayIcon={app}\VideoDownloader.exe >> VideoDownloader_installer.iss
echo Compression=lzma2 >> VideoDownloader_installer.iss
echo SolidCompression=yes >> VideoDownloader_installer.iss
echo OutputDir=installer >> VideoDownloader_installer.iss
echo OutputBaseFilename=VideoDownloader_Setup_v%APP_VERSION% >> VideoDownloader_installer.iss
echo PrivilegesRequired=lowest >> VideoDownloader_installer.iss
echo UsePreviousAppDir=no >> VideoDownloader_installer.iss
echo DirExistsWarning=no >> VideoDownloader_installer.iss

REM Añadir recursos solo si existen
if exist resources\icons\LGA_VideoDownloader.ico (
    echo SetupIconFile=resources\icons\LGA_VideoDownloader.ico >> VideoDownloader_installer.iss
)

echo. >> VideoDownloader_installer.iss
echo [Files] >> VideoDownloader_installer.iss
REM Las tools que el auto-update mantiene (yt-dlp y deno) viven en {app}\tools y se actualizan
REM ahi mismo: si el instalador las pisara, cada update de la app tiraria abajo la version
REM nueva y habria que volver a bajarla. Se instalan SOLO si faltan (onlyifdoesntexist, sin
REM ignoreversion) y quedan excluidas de la copia general. ffmpeg y sus DLL no se auto-
REM actualizan, asi que siguen viniendo con el instalador y se pisan como siempre.
echo Source: "deploy\*"; DestDir: "{app}"; Excludes: "\tools\yt-dlp.exe,\tools\deno.exe"; Flags: ignoreversion recursesubdirs createallsubdirs >> VideoDownloader_installer.iss
echo Source: "deploy\tools\yt-dlp.exe"; DestDir: "{app}\tools"; Flags: onlyifdoesntexist skipifsourcedoesntexist >> VideoDownloader_installer.iss
echo Source: "deploy\tools\deno.exe"; DestDir: "{app}\tools"; Flags: onlyifdoesntexist skipifsourcedoesntexist >> VideoDownloader_installer.iss
REM El script de cierre por ruta viaja dentro del instalador y se extrae a {tmp} en PrepareToInstall.
echo Source: "tools\close_by_path.ps1"; Flags: dontcopy >> VideoDownloader_installer.iss
echo. >> VideoDownloader_installer.iss
REM Al desinstalar, lo que escribio la app en su carpeta (tools actualizadas, tools.json,
REM .staging, .old, las cookies temporales y el instalador que bajo el auto-update) no lo
REM instalo Inno y no se borraria solo.
echo [UninstallDelete] >> VideoDownloader_installer.iss
echo Type: filesandordirs; Name: "{app}\tools" >> VideoDownloader_installer.iss
echo Type: filesandordirs; Name: "{app}\session-cookies" >> VideoDownloader_installer.iss
echo Type: filesandordirs; Name: "{app}\updates" >> VideoDownloader_installer.iss
echo. >> VideoDownloader_installer.iss
echo [Icons] >> VideoDownloader_installer.iss
echo Name: "{group}\VideoDownloader"; Filename: "{app}\VideoDownloader.exe" >> VideoDownloader_installer.iss
echo Name: "{userdesktop}\VideoDownloader"; Filename: "{app}\VideoDownloader.exe"; Tasks: desktopicon >> VideoDownloader_installer.iss
echo. >> VideoDownloader_installer.iss
echo [Tasks] >> VideoDownloader_installer.iss
echo Name: "desktopicon"; Description: "Crear un icono en el escritorio"; GroupDescription: "Iconos adicionales:" >> VideoDownloader_installer.iss
echo. >> VideoDownloader_installer.iss
REM Host de Native Messaging de la extension: una sola clave de HKCU que leen Chrome, Brave y
REM Edge, apuntando al JSON junto al exe. Se borra al desinstalar.
echo [Registry] >> VideoDownloader_installer.iss
REM Los tres padres se borran SOLO si quedan sin subclaves ni valores (uninsdeletekeyifempty):
REM Chrome/Brave/Edge pueden tener otras extensiones con host nativo bajo el mismo
REM NativeMessagingHosts, y Software\Google\Chrome y Software\Google son de Chrome, no nuestros.
REM Van ANTES de la clave propia: Inno deshace el registro en orden inverso, asi que la propia se
REM borra primero y recien despues se revisan los padres, de adentro hacia afuera.
echo Root: HKCU; Subkey: "Software\Google"; ValueType: none; Flags: uninsdeletekeyifempty >> VideoDownloader_installer.iss
echo Root: HKCU; Subkey: "Software\Google\Chrome"; ValueType: none; Flags: uninsdeletekeyifempty >> VideoDownloader_installer.iss
echo Root: HKCU; Subkey: "Software\Google\Chrome\NativeMessagingHosts"; ValueType: none; Flags: uninsdeletekeyifempty >> VideoDownloader_installer.iss
echo Root: HKCU; Subkey: "Software\Google\Chrome\NativeMessagingHosts\com.lga.videodownloader"; ValueType: string; ValueName: ""; ValueData: "{app}\com.lga.videodownloader.json"; Flags: uninsdeletekey >> VideoDownloader_installer.iss
echo. >> VideoDownloader_installer.iss
echo [Run] >> VideoDownloader_installer.iss
REM Sin skipifsilent: las copias v0.95 corren el instalador en /SILENT y tiene que relanzar la app.
REM lgaupdate (v0.96+) lo abre con el wizard: ahi es el checkbox final, tildado por defecto.
echo Filename: "{app}\VideoDownloader.exe"; Description: "Ejecutar VideoDownloader"; Flags: nowait postinstall >> VideoDownloader_installer.iss

REM Añadir código Pascal para preguntar sobre eliminar configuración durante desinstalación
echo. >> VideoDownloader_installer.iss
echo [Code] >> VideoDownloader_installer.iss
REM La app se llamaba VimeoDownloader y el instalador no definia AppId, asi que
REM Inno usaba el AppName como identificador. Al renombrar, la entrada vieja de
REM Programas y caracteristicas queda huerfana apuntando a la carpeta anterior:
REM si el usuario la desinstala desde ahi, borra una instalacion que ya no existe
REM y deja la nueva sin registrar. Se borra la clave y la carpeta vieja.
echo const >> VideoDownloader_installer.iss
echo   LegacyUninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\VimeoDownloader_is1'; >> VideoDownloader_installer.iss
echo   LegacyInstallDir = 'C:\Portable\LGA\VimeoDownloader'; >> VideoDownloader_installer.iss
echo. >> VideoDownloader_installer.iss
REM Bloque de close_by_path de la Base (Doc_Instaladores_Inno.md, seccion 5.1): powershell.exe
REM por su ruta de {sys} [con el nombre suelto se buscaria antes en la carpeta desde la que
REM corre el setup], -NonInteractive [oculto y con espera, un pedido de respuesta colgaria el
REM setup] y las comillas como #34 [una comilla literal en un echo cambia como cmd.exe lee el
REM resto de la linea].
echo procedure CloseByPath(const ScriptPath, Params: String); >> VideoDownloader_installer.iss
echo var >> VideoDownloader_installer.iss
echo   ResultCode: Integer; >> VideoDownloader_installer.iss
echo   CmdLine: String; >> VideoDownloader_installer.iss
echo begin >> VideoDownloader_installer.iss
echo   CmdLine := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' + #34 + ScriptPath + #34 + ' ' + Params; >> VideoDownloader_installer.iss
echo   if Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), CmdLine, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then >> VideoDownloader_installer.iss
echo     Log('close_by_path ' + Params + ': codigo ' + IntToStr(ResultCode)) >> VideoDownloader_installer.iss
echo   else >> VideoDownloader_installer.iss
echo     Log('close_by_path no se pudo ejecutar, no se cierra nada: ' + SysErrorMessage(ResultCode)); >> VideoDownloader_installer.iss
echo end; >> VideoDownloader_installer.iss
echo. >> VideoDownloader_installer.iss
echo function PrepareToInstall(var NeedsRestart: Boolean): String; >> VideoDownloader_installer.iss
echo var >> VideoDownloader_installer.iss
echo   ScriptPath, Prefixes, HelperDirs: String; >> VideoDownloader_installer.iss
echo begin >> VideoDownloader_installer.iss
echo   Result := ''; >> VideoDownloader_installer.iss
REM La app es de instancia unica: al instalar se cierran TODAS las copias de VideoDownloader.exe
REM (la instalada, un build, otro checkout) con sus yt-dlp, deno y ffmpeg, cada uno por la
REM carpeta de su instancia o por las de HelperDirs: {app}, la instalacion legacy y la carpeta de
REM tools de fallback de %LOCALAPPDATA%. Nunca por nombre solo: el yt-dlp o el ffmpeg de otra
REM app se deja vivo (tools\close_by_path.ps1 -AllInstances). -ExeName deja afuera al propio
REM instalador del auto-update, que corre desde {app}\updates. El nombre viejo, VimeoDownloader.exe,
REM se sigue cerrando solo en {app} y en la carpeta legacy. Sin -Tree a proposito: todo lo que
REM lanza la app ya esta en -Helpers, y con -Tree este mismo instalador, lanzado como hijo de la
REM app desde {app}\updates, podia cerrarse a si mismo si la app todavia no habia terminado de
REM salir. Si el script no puede correr o rechaza la ruta, no cierra nada e Inno avisa "archivo
REM en uso".
echo   ExtractTemporaryFile('close_by_path.ps1'); >> VideoDownloader_installer.iss
echo   ScriptPath := ExpandConstant('{tmp}\close_by_path.ps1'); >> VideoDownloader_installer.iss
echo   Prefixes := ExpandConstant('{app}') + ',' + LegacyInstallDir; >> VideoDownloader_installer.iss
echo   HelperDirs := Prefixes + ',' + ExpandConstant('{localappdata}\LGA\VideoDownloader'); >> VideoDownloader_installer.iss
echo   CloseByPath(ScriptPath, '-ExeName VideoDownloader.exe -AllInstances -Helpers yt-dlp.exe,deno.exe,ffmpeg.exe -HelperPrefix ' + #34 + HelperDirs + #34); >> VideoDownloader_installer.iss
echo   CloseByPath(ScriptPath, '-ExeName VimeoDownloader.exe -Prefix ' + #34 + Prefixes + #34); >> VideoDownloader_installer.iss
echo   Sleep(1000); >> VideoDownloader_installer.iss
echo   RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, LegacyUninstallKey); >> VideoDownloader_installer.iss
echo   RegDeleteKeyIncludingSubkeys(HKEY_LOCAL_MACHINE, LegacyUninstallKey); >> VideoDownloader_installer.iss
echo end; >> VideoDownloader_installer.iss
echo. >> VideoDownloader_installer.iss
echo procedure CurStepChanged(CurStep: TSetupStep); >> VideoDownloader_installer.iss
echo begin >> VideoDownloader_installer.iss
echo   if CurStep = ssPostInstall then >> VideoDownloader_installer.iss
echo   begin >> VideoDownloader_installer.iss
echo     if DirExists(LegacyInstallDir) then >> VideoDownloader_installer.iss
echo     begin >> VideoDownloader_installer.iss
echo       if CompareText(LegacyInstallDir, ExpandConstant('{app}')) = 0 then >> VideoDownloader_installer.iss
echo         Exit; >> VideoDownloader_installer.iss
echo       DelTree(LegacyInstallDir, True, True, True); >> VideoDownloader_installer.iss
echo     end; >> VideoDownloader_installer.iss
echo   end; >> VideoDownloader_installer.iss
echo end; >> VideoDownloader_installer.iss
echo. >> VideoDownloader_installer.iss
echo procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep); >> VideoDownloader_installer.iss
echo var >> VideoDownloader_installer.iss
echo   ConfigPath: string; >> VideoDownloader_installer.iss
echo   ResultCode: Integer; >> VideoDownloader_installer.iss
echo begin >> VideoDownloader_installer.iss
echo   if CurUninstallStep = usPostUninstall then >> VideoDownloader_installer.iss
echo   begin >> VideoDownloader_installer.iss
REM Restos de versiones anteriores, que bajaban yt-dlp y deno a LocalAppData (y de una
REM instalacion en carpeta no escribible, donde siguen yendo ahi las tools y el instalador del
REM auto-update): son cache, se borran sin preguntar.
echo     DelTree(ExpandConstant('{localappdata}\LGA\VideoDownloader\tools'), True, True, True); >> VideoDownloader_installer.iss
echo     DelTree(ExpandConstant('{localappdata}\LGA\VideoDownloader\session-cookies'), True, True, True); >> VideoDownloader_installer.iss
echo     DelTree(ExpandConstant('{localappdata}\LGA\VideoDownloader\updates'), True, True, True); >> VideoDownloader_installer.iss
echo     ConfigPath := ExpandConstant('{userappdata}\LGA\VideoDownloader'); >> VideoDownloader_installer.iss
echo     if DirExists(ConfigPath) then >> VideoDownloader_installer.iss
echo     begin >> VideoDownloader_installer.iss
echo       ResultCode := MsgBox('VideoDownloader ha guardado configuración en:' + #13#10 + ConfigPath + #13#10#13#10 + '¿Desea eliminar también esta configuración?', mbConfirmation, MB_YESNO); >> VideoDownloader_installer.iss
echo       if ResultCode = IDYES then >> VideoDownloader_installer.iss
echo       begin >> VideoDownloader_installer.iss
echo         if DelTree(ConfigPath, True, True, True) then >> VideoDownloader_installer.iss
echo           MsgBox('Configuración eliminada correctamente.', mbInformation, MB_OK) >> VideoDownloader_installer.iss
echo         else >> VideoDownloader_installer.iss
echo           MsgBox('No se pudo eliminar completamente la configuración.' + #13#10 + 'Puede eliminarla manualmente desde:' + #13#10 + ConfigPath, mbError, MB_OK); >> VideoDownloader_installer.iss
echo       end; >> VideoDownloader_installer.iss
echo     end; >> VideoDownloader_installer.iss
echo   end; >> VideoDownloader_installer.iss
echo end; >> VideoDownloader_installer.iss

REM Crear directorio para el instalador si no existe
if not exist installer mkdir installer

REM Compilar el instalador
echo Compilando el instalador...
"%INNO_PATH%" VideoDownloader_installer.iss

if %ERRORLEVEL% neq 0 (
    echo Error al compilar el instalador.
    exit /b 1
)

REM SHA256SUMS del release: el auto-update de la app no instala nada sin su hash. Formato
REM sha256sum ("hash  nombre"). Al publicar, el SHA256SUMS del release lleva TAMBIEN las
REM lineas del .zip/.dmg de macOS (deploy/SHA256SUMS de deploy.sh): :publish_release las fusiona.
powershell -NoProfile -Command "$n='VideoDownloader_Setup_v%APP_VERSION%.exe'; $h=(Get-FileHash -Algorithm SHA256 ('installer\'+$n)).Hash.ToLower(); [IO.File]::WriteAllText('installer\SHA256SUMS', $h+'  '+$n+[char]10)"
if %ERRORLEVEL% neq 0 (
    echo Error al generar installer\SHA256SUMS.
    exit /b 1
)
echo SHA256SUMS: installer\SHA256SUMS

echo.
echo Instalador creado exitosamente en la carpeta 'installer'.
echo Archivo: installer\VideoDownloader_Setup_v%APP_VERSION%.exe 
echo. 

REM Ejecutar el instalador recien armado SOLO si lo confirma alguien en una consola. Sin
REM consola (corrida encadenada, automatizada o con la entrada redirigida) el choice podia
REM leer una respuesta de la entrada y terminar ejecutando el instalador; ahora en ese caso no
REM se ejecuta. Si PowerShell no corre, tampoco: no ejecutar es la direccion segura.
if defined NO_RUN (
    echo Instalador no ejecutado ^(--no-run^).
    goto :after_run
)
if not defined INTERACTIVE (
    echo Sin consola interactiva: el instalador no se ejecuta.
    goto :after_run
)
choice /C YN /M "¿Desea ejecutar el instalador ahora mismo?"
if "%ERRORLEVEL%"=="1" (
    echo Ejecutando el instalador...
    REM Lo abre el Explorador de Windows y no esta consola: lanzado con start quedaba dentro del
    REM arbol de procesos de la terminal y se cerraba con ella. Ruta absoluta por SCRIPT_DIR y
    REM APP_VERSION, definidas fuera de este bloque. explorer.exe devuelve siempre 1.
    explorer.exe "%SCRIPT_DIR%installer\VideoDownloader_Setup_v%APP_VERSION%.exe"
) else (
    echo Instalador no ejecutado.
)
:after_run
REM La publicacion ya se decidio antes de armar el instalador [--publish o la opcion 1 del menu].
if /I not "%PUBLISH%"=="always" exit /b 0
call :publish_release
exit /b %ERRORLEVEL%

REM ==== PUBLICAR: inicio. El banco de pruebas extrae lo que hay entre estas marcas: no moverlas.

:gh_detect
REM Sale con 0 y GH_CMD definido si gh esta y tiene login. LGA_GH apunta a otro gh, igual que en
REM el helper de las notas. Va siempre con call: asi tambien sirve un gh que sea un .bat.
set "GH_CMD="
if defined LGA_GH set "GH_CMD=%LGA_GH%"
if not defined GH_CMD for /f "delims=" %%G in ('where gh 2^>nul') do if not defined GH_CMD set "GH_CMD=%%G"
if not defined GH_CMD if exist "C:\Program Files\GitHub CLI\gh.exe" set "GH_CMD=C:\Program Files\GitHub CLI\gh.exe"
if not defined GH_CMD exit /b 1
call "%GH_CMD%" auth status >nul 2>nul
exit /b %ERRORLEVEL%

:git_preflight
REM El tag se crea sobre HEAD (--target): tiene que ser el codigo commiteado y estar en origin.
REM Un archivo sin seguimiento tambien cuenta, igual que en la Mac. Cada corte deja PUB_BLOCKER
REM para el menu de publicacion.
set "DIRTY="
for /f "delims=" %%S in ('git status --porcelain 2^>nul') do set "DIRTY=1"
if defined DIRTY (
    echo ERROR: hay cambios sin commitear; el release tiene que salir de un commit:
    git status --short
    set "PUB_BLOCKER=hay cambios sin commitear [el release tiene que salir de un commit]"
    exit /b 1
)
git fetch -q origin
if errorlevel 1 (
    echo ERROR: no se pudo leer origin.
    set "PUB_BLOCKER=no se pudo leer origin"
    exit /b 1
)
git merge-base --is-ancestor HEAD origin/main
if errorlevel 1 (
    echo ERROR: HEAD no esta en origin/main. Pushear antes de publicar.
    set "PUB_BLOCKER=HEAD no esta en origin/main [pushear antes de publicar]"
    exit /b 1
)
REM VERSION (de donde sale el numero del instalador) tiene que coincidir con CMakeLists.txt.
call "%SCRIPT_DIR%sync_version.bat" --check-only
if errorlevel 1 (
    echo ERROR: VERSION y CMakeLists.txt no coinciden. Correr sync_version.bat y commitear.
    set "PUB_BLOCKER=VERSION y CMakeLists.txt no coinciden"
    exit /b 1
)
set "HEAD_SHA="
for /f %%C in ('git rev-parse HEAD') do set "HEAD_SHA=%%C"
exit /b 0

:notes_check
REM Notas para el usuario [What's new]: la logica vive en LGA_RepoTools (WhatsNew_Shared);
REM LGA_REPOTOOLS apunta a otra copia. Si corta, no se armo ni se publico nada.
set "WN_REPOTOOLS=%LGA_REPOTOOLS%"
if not defined WN_REPOTOOLS set "WN_REPOTOOLS=%SCRIPT_DIR%..\LGA_RepoTools"
set "WN_BAT=%WN_REPOTOOLS%\WhatsNew_Win\whats_new_release.bat"
set "WN_FILE=%SCRIPT_DIR%docs\WhatsNew.md"
if not exist "%WN_BAT%" (
    echo ERROR: no encontre "%WN_BAT%".
    echo Clonar LGA_RepoTools al lado de este repo o definir LGA_REPOTOOLS. Sin notas no se publica.
    exit /b 1
)
echo Verificando las notas para el usuario [What's new] de v%APP_VERSION%...
call "%WN_BAT%" check "%WN_FILE%" "%APP_VERSION%"
if errorlevel 1 (
    echo ERROR: faltan o fallan las notas de v%APP_VERSION%, o se contesto que no. No se armo nada.
    exit /b 1
)
exit /b 0

:release_check
REM Lo que hay en el release v(version) antes de tocarlo. Corre antes de armar nada y otra vez al
REM publicar, porque la Mac pudo publicar en el medio. Corta si el release es un borrador, si ya
REM tiene el instalador de Windows (salvo --replace) o si tiene assets de macOS sin SHA256SUMS:
REM fusionar contra nada borraria sus lineas, y el auto-update de la Mac dejaria de verlo.
call :release_state
if errorlevel 1 exit /b 1
if "%REL_DRAFT%"=="1" (
    echo ERROR: el release %TAG% existe como borrador. Publicarlo o borrarlo a mano antes de seguir.
    exit /b 1
)
if "%REL_OWN%"=="1" if not defined REPLACE (
    echo ERROR: el release %TAG% ya tiene %VD_OWN_ASSET%. Para reemplazarlo, correr con --replace.
    exit /b 1
)
if "%REL_OTHER%"=="1" if "%REL_SUMS%"=="0" (
    echo ERROR: el release %TAG% tiene los archivos de macOS pero no SHA256SUMS: no se fusiona contra
    echo        nada, porque se perderian sus lineas. Subir primero el SHA256SUMS de la Mac.
    exit /b 1
)
exit /b 0

:release_state
REM Solo lee el release v(version): deja REL_EXISTS, REL_DRAFT, REL_SUMS, REL_OWN y REL_OTHER. Sale
REM con 1 si no lo pudo leer. Lo usan :release_check y el menu de publicacion.
set "TAG=v%APP_VERSION%"
set "REL_EXISTS="
set "REL_DRAFT=0"
set "REL_SUMS=0"
set "REL_OWN=0"
set "REL_OTHER=0"
call "%GH_CMD%" release view "%TAG%" --repo "%RELEASE_REPO%" >nul 2>nul
if errorlevel 1 exit /b 0
set "REL_EXISTS=1"
set "VD_RC=%TEMP%\vd_release_check_%RANDOM%%RANDOM%"
mkdir "%VD_RC%" >nul 2>nul
REM Las respuestas de gh van a archivos: un gh entre comillas adentro de un for /f no itera. Se
REM leen con PowerShell porque gh escribe solo LF y findstr /X no las reconoce.
call "%GH_CMD%" release view "%TAG%" --repo "%RELEASE_REPO%" --json isDraft -q ".isDraft" > "%VD_RC%\draft.txt"
if errorlevel 1 goto :release_state_gh_failed
call "%GH_CMD%" release view "%TAG%" --repo "%RELEASE_REPO%" --json assets -q ".assets[].name" > "%VD_RC%\assets.txt"
if errorlevel 1 goto :release_state_gh_failed
set "VD_OWN_ASSET=VideoDownloader_Setup_v%APP_VERSION%.exe"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$d=$env:VD_RC; $a=@(Get-Content -LiteralPath (Join-Path $d 'assets.txt') | ForEach-Object { $_.Trim() }); $draft=@(Get-Content -LiteralPath (Join-Path $d 'draft.txt') | ForEach-Object { $_.Trim() }) -contains 'true'; $other=@($a | Where-Object { $_ -like 'LGA_Video_Downloader_Mac_v*' }).Count -gt 0; Set-Content -LiteralPath (Join-Path $d 'state.txt') -Encoding Ascii -Value ('REL_DRAFT=' + [int]$draft), ('REL_SUMS=' + [int]($a -contains 'SHA256SUMS')), ('REL_OWN=' + [int]($a -contains $env:VD_OWN_ASSET)), ('REL_OTHER=' + [int]$other)"
if errorlevel 1 goto :release_state_gh_failed
for /f "usebackq tokens=1,2 delims==" %%A in ("%VD_RC%\state.txt") do set "%%A=%%B"
rmdir /S /Q "%VD_RC%" >nul 2>nul
exit /b 0
:release_state_gh_failed
echo ERROR: no se pudo leer el release %TAG% de %RELEASE_REPO%.
set "PUB_BLOCKER=no se pudo leer el release %TAG% de %RELEASE_REPO%"
rmdir /S /Q "%VD_RC%" >nul 2>nul
exit /b 1

:publish_release
REM El release v(version) vive en este mismo repo. Lo crea la primera plataforma que publica; la
REM otra (deploy.sh --publish en la Mac) lo encuentra y suma lo suyo. SHA256SUMS es UNO para las
REM dos y el auto-update exige la linea de su asset: se baja el del release, se reemplaza solo la
REM linea del instalador de Windows y se resube. Todo lo que puede cortar (leer, fusionar) pasa
REM antes de escribir nada. El .exe sube ANTES que el SHA256SUMS: en el medio la app no ofrece
REM nada que no pueda verificar.
call :release_check
if errorlevel 1 exit /b 1
set "ASSET=VideoDownloader_Setup_v%APP_VERSION%.exe"
set "WORK="
if not defined REL_EXISTS goto :publish_create
echo El release %TAG% ya existe: se suma el instalador de Windows.
set "WORK=%TEMP%\vd_release_%RANDOM%%RANDOM%"
mkdir "%WORK%" >nul 2>nul
if "%REL_SUMS%"=="0" goto :publish_merge
call "%GH_CMD%" release download "%TAG%" --repo "%RELEASE_REPO%" --pattern SHA256SUMS --dir "%WORK%"
if errorlevel 1 goto :publish_failed
:publish_merge
REM Una sola linea por nombre: las del release que no son el instalador de Windows, y la propia.
REM Una linea ilegible corta: no se adivina que era. El resultado queda en una ruta fija para
REM poder resubirlo a mano si la subida falla (--clobber borra el viejo antes de subir).
mkdir "installer\release" >nul 2>nul
set "VD_OLD=%WORK%\SHA256SUMS"
set "VD_OWN=installer\SHA256SUMS"
set "VD_OUT=installer\release\SHA256SUMS"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $re='^([0-9a-fA-F]{64}) [ *](.+)$'; $own=@(([IO.File]::ReadAllText($env:VD_OWN)) -split '\r?\n' | Where-Object { $_ -match $re }); if ($own.Count -ne 1) { Write-Host 'ERROR: installer\SHA256SUMS no tiene una sola linea valida'; exit 1 }; $null=$own[0] -match $re; $seen=@{}; $seen[$Matches[2]]=1; $keep=New-Object System.Collections.Generic.List[string]; if (Test-Path -LiteralPath $env:VD_OLD) { foreach ($l in (([IO.File]::ReadAllText($env:VD_OLD)) -split '\r?\n')) { if ($l -match $re) { if (-not $seen.ContainsKey($Matches[2])) { $seen[$Matches[2]]=1; $keep.Add($l) } } elseif ($l.Trim()) { Write-Host ('ERROR: el SHA256SUMS del release tiene una linea ilegible: ' + $l); exit 1 } } }; $keep.Add($own[0]); [IO.File]::WriteAllText($env:VD_OUT, (($keep -join [char]10) + [char]10)); Write-Host ('SHA256SUMS fusionado: ' + $keep.Count + ' lineas')"
if errorlevel 1 goto :publish_failed
call "%GH_CMD%" release upload "%TAG%" "installer\%ASSET%" --repo "%RELEASE_REPO%" --clobber
if errorlevel 1 goto :publish_failed
call "%GH_CMD%" release upload "%TAG%" "%VD_OUT%" --repo "%RELEASE_REPO%" --clobber
if errorlevel 1 goto :publish_sums_failed
rmdir /S /Q "%WORK%" >nul 2>nul
goto :publish_notes
:publish_create
echo Creando el release %TAG% en %RELEASE_REPO%...
call "%GH_CMD%" release create "%TAG%" "installer\%ASSET%" "installer\SHA256SUMS" --repo "%RELEASE_REPO%" --target "%HEAD_SHA%" --title "%TAG%" --notes "LGA Video Downloader %TAG%"
if errorlevel 1 goto :publish_failed
:publish_notes
echo Publicando las notas para el usuario [What's new]...
call "%WN_BAT%" publish "%WN_FILE%" "%APP_VERSION%" "%RELEASE_REPO%" "%TAG%"
if errorlevel 1 (
    echo ERROR: el release %TAG% quedo publicado, pero sus notas no. Reintentar con el comando de arriba.
    exit /b 1
)
echo Release publicado: https://github.com/%RELEASE_REPO%/releases/tag/%TAG%
REM El auto-update lee el manifiesto de legandrop/LGA_Updates: sin este disparo, la version nueva
REM tarda hasta que corra su cron (30-45 min). Si falla, el cron la levanta igual.
call "%GH_CMD%" workflow run refresh_versions.yml --repo legandrop/LGA_Updates >nul 2>nul <nul
if errorlevel 1 (
    echo AVISO: No se pudo disparar el refresco del manifiesto. El cron lo levanta solo.
) else (
    echo Manifiesto de versiones: refresco disparado.
)
exit /b 0
:publish_sums_failed
echo ERROR: el instalador subio, pero SHA256SUMS no: el release puede haber quedado SIN SHA256SUMS
echo        y la app no ofrece el update. El fusionado esta en %CD%\%VD_OUT%. Subirlo con:
echo        gh release upload %TAG% "%CD%\%VD_OUT%" --repo %RELEASE_REPO% --clobber
if defined WORK rmdir /S /Q "%WORK%" >nul 2>nul
exit /b 1
:publish_failed
echo ERROR: fallo la publicacion del release %TAG%. Ver el mensaje de arriba.
if defined WORK rmdir /S /Q "%WORK%" >nul 2>nul
exit /b 1

REM ==== PUBLICAR: fin

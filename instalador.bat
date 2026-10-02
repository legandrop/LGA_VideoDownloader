@echo off
setlocal
REM Uso: instalador.bat [--no-run] [--publish o --no-publish]
REM   --no-run      arma el instalador y su SHA256SUMS, y no pregunta si ejecutarlo.
REM   --publish     publica sin preguntar el release v(version) en legandrop/LGA_VideoDownloader:
REM                 el instalador, SHA256SUMS y las notas para el usuario (What's new).
REM   --no-publish  no publica ni pregunta.
REM Sin flags de publicacion: con consola pregunta al final; sin consola no publica.
set "NO_RUN="
set "PUBLISH=prompt"
set "SCRIPT_DIR=%~dp0"
:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="--no-run" ( set "NO_RUN=1" & shift & goto :parse_args )
if /I "%~1"=="--publish" ( set "PUBLISH=always" & shift & goto :parse_args )
if /I "%~1"=="--no-publish" ( set "PUBLISH=never" & shift & goto :parse_args )
echo Error: opcion desconocida: %~1
exit /b 1
:args_done

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
REM origen solo cuando es mas nuevo que el destino, y no copia nada.
set "DEPLOY_STALE="
if exist build\VideoDownloader.exe (
    for /f "delims=" %%A in ('xcopy build\VideoDownloader.exe deploy\ /L /D /Y 2^>nul ^| find /i "VideoDownloader.exe"') do set "DEPLOY_STALE=1"
)
if defined DEPLOY_STALE (
    echo Error: deploy\VideoDownloader.exe es mas viejo que build\VideoDownloader.exe
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

REM ---- Publicacion: lo que puede cortarla se chequea ACA, antes de armar el instalador ----
REM El release vive en este mismo repo. Descubrir al final un gh sin login o notas que faltan
REM es tirar la corrida. Sin consola no hay a quien preguntarle: sin --publish no se publica.
set "RELEASE_REPO=legandrop/LGA_VideoDownloader"
set "INTERACTIVE="
powershell -NoProfile -Command "if ([Console]::IsInputRedirected) { exit 3 } else { exit 0 }"
if not errorlevel 1 set "INTERACTIVE=1"
if /I "%PUBLISH%"=="prompt" if not defined INTERACTIVE set "PUBLISH=never"
if /I "%PUBLISH%"=="never" goto :publish_preflight_done
call :gh_detect
if not errorlevel 1 goto :publish_gh_ok
if /I "%PUBLISH%"=="always" (
    echo ERROR: se pidio --publish pero gh no esta disponible o no tiene login.
    exit /b 1
)
echo AVISO: gh no esta disponible o no tiene login: se arma el instalador sin publicar.
set "PUBLISH=never"
goto :publish_preflight_done
:publish_gh_ok
call :git_preflight
if errorlevel 1 exit /b 1
call :notes_check
if errorlevel 1 exit /b 1
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
REM Sin skipifsilent: el auto-update corre el instalador en /SILENT y tiene que relanzar la app.
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
    start "" "installer\VideoDownloader_Setup_v%APP_VERSION%.exe"
) else (
    echo Instalador no ejecutado.
)
:after_run
if /I "%PUBLISH%"=="never" exit /b 0
if /I "%PUBLISH%"=="always" goto :do_publish
choice /C YN /M "Publicar el release v%APP_VERSION% en %RELEASE_REPO%"
if errorlevel 2 (
    echo Release no publicado.
    exit /b 0
)
:do_publish
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
REM -uno y no --untracked-files=no: adentro de un for /f el = se vuelve espacio, git toma el "no"
REM como una ruta y nunca veia cambios.
set "DIRTY="
for /f "delims=" %%S in ('git status --porcelain -uno 2^>nul') do set "DIRTY=1"
if defined DIRTY (
    echo ERROR: hay cambios sin commitear; el release tiene que salir de un commit:
    git status --short -uno
    exit /b 1
)
git fetch -q origin
if errorlevel 1 (
    echo ERROR: no se pudo leer origin.
    exit /b 1
)
git merge-base --is-ancestor HEAD origin/main
if errorlevel 1 (
    echo ERROR: HEAD no esta en origin/main. Pushear antes de publicar.
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

:publish_release
REM El release v(version) vive en este mismo repo. Lo crea la primera plataforma que publica; la
REM otra (deploy.sh --publish en la Mac) lo encuentra y suma lo suyo. SHA256SUMS es UNO para las
REM dos y el auto-update exige la linea de su asset: al sumarse se baja el del release, se
REM reemplaza solo la linea del instalador de Windows y se resube. El .exe sube ANTES que el
REM SHA256SUMS: en el medio la app no ofrece nada que no pueda verificar.
set "TAG=v%APP_VERSION%"
set "ASSET=VideoDownloader_Setup_v%APP_VERSION%.exe"
set "WORK=%TEMP%\vd_release_%RANDOM%%RANDOM%"
mkdir "%WORK%\up" >nul 2>nul
call "%GH_CMD%" release view "%TAG%" --repo "%RELEASE_REPO%" >nul 2>nul
if errorlevel 1 goto :publish_create
echo El release %TAG% ya existe: se suma el instalador de Windows.
call "%GH_CMD%" release upload "%TAG%" "installer\%ASSET%" --repo "%RELEASE_REPO%" --clobber
if errorlevel 1 goto :publish_failed
REM La lista va a un archivo: un gh entre comillas adentro de un for /f no itera.
call "%GH_CMD%" release view "%TAG%" --repo "%RELEASE_REPO%" --json assets -q ".assets[].name" > "%WORK%\assets.txt"
if errorlevel 1 goto :publish_failed
findstr /X /C:"SHA256SUMS" "%WORK%\assets.txt" >nul
if errorlevel 1 goto :publish_merge
REM Si el release trae SHA256SUMS, bajarlo es obligatorio: fusionar contra nada borraria las
REM lineas de la Mac.
call "%GH_CMD%" release download "%TAG%" --repo "%RELEASE_REPO%" --pattern SHA256SUMS --dir "%WORK%"
if errorlevel 1 goto :publish_failed
:publish_merge
REM Una sola linea por nombre: las del release que no son el instalador de Windows, y la propia.
set "VD_OLD=%WORK%\SHA256SUMS"
set "VD_OWN=installer\SHA256SUMS"
set "VD_OUT=%WORK%\up\SHA256SUMS"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $re='^([0-9a-fA-F]{64}) [ *](.+)$'; $own=@(([IO.File]::ReadAllText($env:VD_OWN)) -split '\r?\n' | Where-Object { $_ -match $re }); if ($own.Count -ne 1) { Write-Host 'ERROR: installer\SHA256SUMS no tiene una sola linea valida'; exit 1 }; $null=$own[0] -match $re; $seen=@{}; $seen[$Matches[2]]=1; $keep=New-Object System.Collections.Generic.List[string]; if (Test-Path -LiteralPath $env:VD_OLD) { foreach ($l in (([IO.File]::ReadAllText($env:VD_OLD)) -split '\r?\n')) { if ($l -match $re) { if (-not $seen.ContainsKey($Matches[2])) { $seen[$Matches[2]]=1; $keep.Add($l) } } elseif ($l.Trim()) { Write-Host ('AVISO: linea ilegible descartada: ' + $l) } } }; $keep.Add($own[0]); [IO.File]::WriteAllText($env:VD_OUT, (($keep -join [char]10) + [char]10)); Write-Host ('SHA256SUMS fusionado: ' + $keep.Count + ' lineas')"
if errorlevel 1 goto :publish_failed
call "%GH_CMD%" release upload "%TAG%" "%VD_OUT%" --repo "%RELEASE_REPO%" --clobber
if errorlevel 1 goto :publish_failed
goto :publish_notes
:publish_create
echo Creando el release %TAG% en %RELEASE_REPO%...
call "%GH_CMD%" release create "%TAG%" "installer\%ASSET%" "installer\SHA256SUMS" --repo "%RELEASE_REPO%" --target "%HEAD_SHA%" --title "%TAG%" --notes "LGA Video Downloader %TAG%"
if errorlevel 1 goto :publish_failed
:publish_notes
rmdir /S /Q "%WORK%" >nul 2>nul
echo Publicando las notas para el usuario [What's new]...
call "%WN_BAT%" publish "%WN_FILE%" "%APP_VERSION%" "%RELEASE_REPO%" "%TAG%"
if errorlevel 1 (
    echo ERROR: el release %TAG% quedo publicado, pero sus notas no. Reintentar con el comando de arriba.
    exit /b 1
)
echo Release publicado: https://github.com/%RELEASE_REPO%/releases/tag/%TAG%
exit /b 0
:publish_failed
echo ERROR: fallo la publicacion del release %TAG%. No se deshizo nada; ver el mensaje de gh.
rmdir /S /Q "%WORK%" >nul 2>nul
exit /b 1

REM ==== PUBLICAR: fin

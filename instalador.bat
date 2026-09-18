@echo off
REM Uso: instalador.bat [--no-run]
REM   --no-run  arma el instalador y su SHA256SUMS, y no pregunta si ejecutarlo.
set "NO_RUN="
if /I "%~1"=="--no-run" set "NO_RUN=1"

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
echo   ScriptPath, Prefixes: String; >> VideoDownloader_installer.iss
echo begin >> VideoDownloader_installer.iss
echo   Result := ''; >> VideoDownloader_installer.iss
REM Se cierran SOLO las copias que corren desde la carpeta de destino o desde la instalacion
REM legacy, por la ruta real del proceso (tools\close_by_path.ps1). Antes era un taskkill por
REM nombre, que cerraba tambien cualquier otra copia: un build, otro checkout. -ExeName deja
REM afuera al propio instalador del auto-update, que corre desde {app}\updates. Si el script
REM no puede correr o rechaza la ruta, no cierra nada e Inno avisa "archivo en uso".
echo   ExtractTemporaryFile('close_by_path.ps1'); >> VideoDownloader_installer.iss
echo   ScriptPath := ExpandConstant('{tmp}\close_by_path.ps1'); >> VideoDownloader_installer.iss
echo   Prefixes := ExpandConstant('{app}') + ',' + LegacyInstallDir; >> VideoDownloader_installer.iss
echo   CloseByPath(ScriptPath, '-ExeName VideoDownloader.exe -Prefix ' + #34 + Prefixes + #34); >> VideoDownloader_installer.iss
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
REM lineas del .zip/.dmg de macOS (deploy/SHA256SUMS de deploy.sh).
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
    exit /b 0
)
powershell -NoProfile -Command "if ([Console]::IsInputRedirected) { exit 3 } else { exit 0 }"
if errorlevel 1 (
    echo Sin consola interactiva: el instalador no se ejecuta.
    exit /b 0
)
choice /C YN /M "¿Desea ejecutar el instalador ahora mismo?"
if "%ERRORLEVEL%"=="1" (
    echo Ejecutando el instalador...
    start "" "installer\VideoDownloader_Setup_v%APP_VERSION%.exe"
) else (
    echo Instalador no ejecutado.
)
exit /b 0

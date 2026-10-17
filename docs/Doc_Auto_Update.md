# Actualizacion automatica de la app

LGA Video Downloader se actualiza con `libs/lgaupdate`, la misma biblioteca de FrameRev, Media
Tools y Theta Explorer: copia IDENTICA de la pieza canonica de `../LGA_Base_QT_C_Py` (contrato en
`libs/lgaupdate/README.md`). No se edita aca: un cambio va a la Base y se re-copia. Hasta la v0.95
la app tenia un updater propio (`UpdateService`) que en macOS solo abria la pagina del release.

Esto es el update de la APP. yt-dlp y deno se actualizan aparte, con `ToolsUpdater`.

```text
manifiesto: https://legandrop.github.io/LGA_Updates/versions.json
producto:   legandrop/LGA_VideoDownloader   (repo publico de codigo y de releases)
Windows:    VideoDownloader_Setup_v<version>.exe     (instalador.bat)
macOS:      LGA_Video_Downloader_Mac_v<version>.zip  (deploy.sh --zip)
```

## Recorrido del usuario

- 3,5 s despues del arranque, si la app se actualizo sin que el usuario viera las notas de la
  version nueva (`Updates/WhatsNewLastSeen` en `config.ini`), las muestra una vez. En una
  instalacion nueva no muestra nada.
- A los 7 s chequea, y despues cada 3 horas (+/-15 min) mientras siga abierta. Si hay una version
  nueva aparece el cartel **Update Available** con las notas de todo lo que cambio entre la
  instalada y la ofrecida, y cuatro caminos: **Update now**, **Remind me later** (un dia), **Skip
  this version** y **Cancel** (calla esa version hasta reiniciar la app).
- El header muestra **Update available · v<x>** desde que un chequeo encuentra la version nueva,
  aunque el cartel se haya pospuesto. Click: el mismo cartel, con las notas.
- **Settings**: la caja del update (Update / Later) y la fila **Updates** con **Check now** (sin hora
  del ultimo chequeo: lgaupdate no informa si un chequeo fallo, y un chequeo automatico fallido no
  muestra nada). **Help**: el link **What's new**, que abre el historial completo (sin red, la ultima
  copia verificada).
- **Update now** con descargas en la cola no instala: avisa que hay que dejarlas terminar o
  cancelarlas. Se vuelve a mirar al terminar de bajar el update.
- La descarga muestra progreso y se puede cancelar. Despues la app se cierra sola:
  - **Windows**: un helper de PowerShell espera a que se cierre `VideoDownloader.exe` de ESA
    carpeta (nunca fuerza el cierre) y lanza el instalador con `/DIR=<carpeta actual>`, **con su
    wizard** y no en silencio: es lo que hace lgaupdate en todas las apps (la v0.95 lo corria con
    `/SILENT`). El usuario confirma la instalacion; el `[Run]` del instalador (tildado por defecto)
    vuelve a abrir la app. Si cancela el wizard, la app queda cerrada y la version vieja intacta.
    Instalar en silencio o no es una decision de la pieza canonica de lgaupdate, no de esta app.
  - **macOS**: un helper de bash extrae el ZIP con `ditto`, verifica la firma
    (`codesign --verify --deep --strict`), mueve el bundle actual a un backup, copia el nuevo,
    vuelve a verificar la firma, saca la cuarentena y abre la app nueva. Si algo falla restaura el
    backup, reabre la app que habia y avisa con una notificacion. Log:
    `~/Library/Logs/LGA/LGA_Video_Downloader/Update.log`.
- Un build del arbol de desarrollo nunca se actualiza (`LgaRegistry::isDevelopmentBuild`): Settings
  lo dice y el chequeo manual tambien.

## Implementacion

Todo se configura en `MainWindow::setupUpdates()`:

- `productId` identifica los ASSETS y no es el mismo en las dos plataformas:
  `LGA_Video_Downloader` en macOS y `VideoDownloader` en Windows (el instalador no se renombra,
  decision del 2026-10-06). El bundle se llama `LGA Video Downloader.app`: lgaupdate lo resuelve en
  runtime y busca el `.app` del ZIP por glob, asi que el nombre no tiene que coincidir con el
  `productId`.
- `settingsPath` es el `config.ini` de siempre. Ahi viven `Updates/SnoozeUntil`,
  `Updates/SkippedVersion` y `Updates/WhatsNewLastSeen`; la cache verificada de las notas queda al
  lado (`whats_new_legandrop_LGA_VideoDownloader.json`). La v0.95 publicada no guardaba que notas
  se vieron: quien actualiza desde ella cuenta como instalacion nueva y no ve un "What's new" al
  arrancar (ya las vio en el cartel del update si venia de una 0.96 o posterior).
- `updateCacheDir` es la carpeta de arriba de `AppPaths::heavyDataDir("updates")`: el update baja
  a `<carpeta del exe>\updates` en Windows (o su fallback de `%LOCALAPPDATA%`) y a
  `~/Library/Application Support/LGA/VideoDownloader/updates` en macOS, fuera del bundle.
- Al arrancar, fuera del hilo de UI, `LgaUpdateService::sweepUpdateCache` vacia esa carpeta (el
  instalador o el ZIP del update anterior no los borra nadie: la app ya se cerro) y
  `AppPaths::sweepLegacyTempInstallers` barre `%TEMP%\LGA_VideoDownloader_updates`, donde bajaban
  las versiones <= 0.95. A los 5 minutos se barre otra vez, en el hilo de UI y solo si no hay un
  update en curso: en Windows el instalador todavia corre cuando abre la app nueva y no se puede
  borrar en el primer barrido. No barren ni los builds de desarrollo ni los modos de QA: en macOS la
  carpeta es la misma de la copia instalada.
- `prepareForUpdatePrompt` cierra Help y Settings: el cartel sale sobre la ventana principal, con su velo.
- `quitApplication` corta la cola (si quedo algo) y cierra la app.
- `presentDialog` dibuja los carteles de la biblioteca con `ConfirmDialog`
  (`include/videodownloader/ConfirmDialog.h`), con el aspecto de Help: `fromUpdateSpec` traduce el
  `LgaUpdateDialogSpec` (mensaje, notas, detalle, botones) a la spec del dialogo.

## Probar sin publicar

`LGA_VD_GITHUB_BASE=http://127.0.0.1:<puerto>` (solo acepta localhost) desvia el manifiesto y los
assets a un servidor local con las mismas rutas: `/LGA_Updates/versions.json` y
`/legandrop/LGA_VideoDownloader/releases/download/v<x>/<asset>`. El manifiesto de prueba lleva el
producto con `assetLatest` y el SHA-256 real del asset y de `whats_new.json`. Con
`LGA_VD_QA_ACCEPT_UPDATE=1` ademas, la app aprieta **Update now** sola.

Prueba hecha en macOS el 2026-10-15: copia 0.95 (deploy firmado) en una carpeta temporal, ZIP 0.96
de `deploy.sh --zip --no-install` servido en local. La app ofrecio el update con las notas, bajo
el ZIP, el helper verifico la firma, reemplazo el bundle (firma valida despues del cambio, sin
backup sobrante) y abrio la 0.96, que volvio a chequear y no ofrecio nada. Config, registro LGA y
manifiestos del host de la extension respaldados antes y restaurados despues.
Repetida el mismo dia con la `libs/lgaupdate` canonica de la Base: la 0.95 aprieta **Update now**
por el cartel de la app (`presentDialog`), baja y verifica el ZIP, y el helper reemplaza el bundle
(firma valida, sin backup) y reabre la 0.96. La copia de prueba se anota en el registro LGA
(`~/Library/Application Support/LGA/VideoDownloader.json`, con su ruta temporal): borrarlo al
restaurar.

`--ui-shot` dibuja los carteles: `update-prompt` (con `--notes <whats_new.json>`), `whats-new`
(necesita `--notes`) y `update-failed`; `settings` y `settings-update` muestran Settings, sin y con la
caja del update.

## La extension de navegador y el update

La extension no se actualiza sola ni por separado: viaja con la app y no necesita nada especial al
actualizar. Lo que hay que saber:

- **El host es el mismo ejecutable.** El navegador lo lanza con `chrome-extension://<ID>/`: lee UN
  mensaje, lo pasa a la ventana abierta por `QLocalSocket` (si no hay, abre la app) y sale. Vive
  poco: hasta 5 s esperando el mensaje y hasta 24 s en total. La extension usa
  `sendNativeMessage` (un mensaje, una respuesta), no una conexion abierta.
- **Registro.** macOS: un JSON con la ruta ABSOLUTA al ejecutable en la carpeta
  `NativeMessagingHosts` de Chrome, Brave y Edge, que la app reescribe en cada arranque si no
  coincide. El update de Mac reemplaza el bundle en la MISMA ruta, asi que el JSON sigue valido; la
  app reabierta lo vuelve a verificar. Windows: clave en HKCU + JSON junto al exe con ruta relativa;
  el instalador reinstala en la misma carpeta (`/DIR`) y la app repara la clave al arrancar.
- **Version.** La extension es `1.0.0` y habla el protocolo v1, igual que la app; el host contesta
  con la version de la app (`"app"`) y de la extension. En macOS la app copia la extension de su
  bundle a Application Support cuando cambia de version; Brave sigue con la cargada hasta que se
  recarga (Load unpacked no se actualiza solo). Si un update cambia el protocolo, va con su propia
  version del mensaje (`unsupported_version`).
- **Un host vivo durante el update** cuenta como app abierta para el helper (mismo ejecutable).
  Como vive pocos segundos, solo demora el reemplazo; el helper espera 30 s en Mac y 15 s en
  Windows. Caso borde no probado: si el usuario manda un video justo en el segundo en que la app ya
  se cerro y el helper todavia no termino, el host abre la app vieja; el helper la ve abierta y
  posterga el update sin forzar nada (el video llega a la app vieja; el update se vuelve a ofrecer).

Probado en Mac el 2026-10-15, llamando al host como lo hace el navegador (la ruta del JSON de Brave,
el origen y el mensaje enmarcado por stdin): antes del update contesto la 0.95 y encolo el link;
despues, el mismo JSON llevo a la 0.96, que encolo en la app reabierta; con la app cerrada, el host
abrio la 0.96 (`launched: true`). La extension no esta cargada en el Brave de esa Mac (en macOS no
se usa todavia), asi que el click real queda en el pendiente de Windows.

## Contrato de publicacion

- Tag `v<x>` igual a la version de `CMakeLists.txt`.
- Nombres de asset exactos, los de arriba. El manifiesto publica su SHA-256 (lo calcula GitHub);
  sin SHA-256 la app no ofrece el update.
- ZIP de macOS creado con `ditto` desde un bundle firmado y verificado (`deploy.sh` ya lo hace).
- En macOS publica `tools/macos/publish_release.sh`, copia identica de la pieza canonica de
  `../LGA_Base_QT_C_Py` (contrato en su `docs/Doc_Deploy_macOS.md` §5); los nombres, el
  `SHA256SUMS` fusionado con `VideoDownloader_Setup_v<x>.exe` y las notas salen de
  `tools/macos/release.conf`.
- Despues de publicar, `deploy.sh --publish` e `instalador.bat --publish` disparan
  `refresh_versions.yml` de `LGA_Updates`; si el disparo falla, el cron lo levanta en 30-45 min.
- Las copias v0.95 instaladas todavia usan el updater viejo, que lee `SHA256SUMS` del release:
  mientras quede alguna, el release lleva `SHA256SUMS` (los dos scripts lo siguen generando).

## Pendiente

- Probar el update de Windows de punta a punta (instalador Inno, helper de PowerShell, reapertura)
  en la maquina de Windows.

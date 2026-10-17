# LGA Video Downloader

Aplicación Qt/C++ (Windows y macOS) para descargar videos con [yt-dlp](https://github.com/yt-dlp/yt-dlp): YouTube, Vimeo, SoundCloud, Instagram y el resto de los sitios que yt-dlp soporta. Es una interfaz con cola de descargas; el trabajo lo hacen yt-dlp, ffmpeg y deno, que vienen con la app y se actualizan solos.

## Uso

1. Pegá uno o varios links en **Add videos** (uno por renglón, o mezclados con texto: la app se queda solo con los links y no repite los duplicados).
2. Elegí **Format** (MP4 con video y audio, o solo audio M4A), **Quality** (*Most compatible (H.264)*, que abre en cualquier editor, o *Best quality*) y **Save to**.
3. Apretá **Download** (o Ctrl+Enter). Cada link es una tarjeta en **Queue** con su progreso; se descarga de a uno.

- **Sesión (Use cookies from):** no se piden usuario ni contraseña. Para videos privados, de miembros o con restricción de edad se usa la sesión de un navegador donde ya estés logueado. Solo aparecen los navegadores instalados. En Windows, Chrome/Edge/Brave/Opera/Vivaldi cifran sus cookies y no se pueden leer: usá Firefox o un archivo `cookies.txt`.
- **Errores:** la tarjeta explica qué pasó y cómo seguir (por ejemplo "Sign in to confirm your age" con *Retry with Firefox*). Las transmisiones en vivo se rechazan al empezar ("Live streams aren't supported") y los sitios que yt-dlp no soporta terminan en "This site isn't supported".
- **Cancelar:** la X de la tarjeta, *Cancel all* o cerrar la app cortan yt-dlp con sus procesos hijos y borran los archivos parciales de esa descarga (`.part`, `.ytdl`, fragmentos). Nunca archivos completos ni de otras descargas.
- **Log:** siempre visible abajo, con filtros All/Warnings/Errors y botón Copy.
- **Barra de título** propia, la de FaceLock y RotoWorker: icono y nombre a la izquierda, avisos (tools, update), **Settings** y **Help** a la derecha y, en Windows, los botones de ventana (la ventana no tiene barra nativa).
- **Settings** (engranaje): actualizaciones (Check now), cómo instalar la extensión de navegador y versiones de yt-dlp/ffmpeg/deno.
- **Help** (icono `?`): versión, What's new, autor y créditos.

Configuración: `%APPDATA%\LGA\VideoDownloader\config.ini` (Windows) o `~/Library/Application Support/LGA/VideoDownloader/config.ini` (macOS). En AppData solo van settings: lo pesado (yt-dlp, deno y las cookies temporales) vive en la carpeta de la app en Windows y en Application Support en macOS.

## Actualizaciones

- **yt-dlp y deno:** al abrir la app se buscan versiones nuevas en GitHub (tag fijo, SHA-256 obligatorio) y se instalan en la carpeta `tools` de la propia app (Windows) o en `~/Library/Application Support/LGA/VideoDownloader/tools` (macOS, fuera del bundle por la firma). El reemplazo se hace solo cuando no hay una descarga corriendo.
- **La app:** `libs/lgaupdate`, la biblioteca de updates de las apps LGA. Lee el manifiesto de versiones de `LGA_Updates` al abrir y después cada 3 horas (±15 min al azar) mientras sigue abierta. Si hay una versión nueva, ofrece el update en un cartel con las notas de lo nuevo (`whats_new.json` del release) y lo marca con "Update available" arriba a la derecha y en Settings. Baja el instalador (Windows) o el ZIP firmado (macOS), verifica el SHA-256 y lo instala sobre la misma carpeta. **What's new** en Help muestra el historial completo, y después de un update que no se vio venir la app muestra sus notas una vez. Detalle en `docs/Doc_Auto_Update.md`.
- **ffmpeg** viene con la app y no se actualiza solo.

Detalle por plataforma: [PLATFORM_DIFFERENCES.md](PLATFORM_DIFFERENCES.md). Cola y parseo de yt-dlp: [DOWNLOAD_QUEUE_SYSTEM.md](DOWNLOAD_QUEUE_SYSTEM.md).

## Compilación

Requisitos: Qt 6.8.2, CMake 3.16+, compilador C++17.

```bash
./compilar.bat      # Windows (siempre este script, no cmake a mano)
./compilar.sh       # macOS
./deploy.bat        # Windows: versión portable
./deploy.sh --zip --dmg   # macOS: .zip de actualización y .dmg de instalación
./deploy_local.sh   # macOS: compila en Release, instala en /Applications y abre, sin publicar
instalador_local.bat      # Windows: compila y arma el instalador, sin Git ni GitHub
```

### Versión y bump

La versión va de a milésimos (`1.000`, `1.001`...) y se sube a mano. `CMakeLists.txt` y `VERSION` guardan la última versión **publicada**; el primer `vX.YYY:` de `docs/ChangeLog.md` es la versión **en desarrollo**, donde se juntan las entradas hasta el próximo release. `bump_version.bat` (macOS: `./bump_version.sh`) sube `CMakeLists.txt` y `VERSION` a la del changelog, commitea, pushea y ofrece publicar; `--check` solo verifica que los tres coincidan y que el bump esté commiteado. Sin Python: la lógica está en `tools/bump_version.ps1` y en el `.sh`.

### Publicar un release

```bash
instalador.bat                  # Windows: menú de una tecla antes de compilar
instalador.bat --publish        # Windows: compila, arma el instalador y publica
./deploy.sh --publish           # macOS: compila, arma el .zip y el .dmg y publica
./deploy.sh --publish --dry-run # macOS: recorre todo sin escribir en GitHub ni en el repo
```

`instalador.bat` compila solo (`deploy.bat --no-run`) después de decidir la publicación. Sin `--publish` ni `--local`, `instalador.bat` y `./deploy.sh` (si hay consola; si no, arman solo lo local sin preguntar) miran el release antes de compilar y ofrecen con una tecla publicar o sumar los archivos de su plataforma, el bump y publicar si el changelog va adelante, instalador o build local, o cancelar; si algo impide publicar, lo dicen y ofrecen solo lo local o cancelar. `--no-publish` no pregunta. Cada publicación valida las notas para el usuario (`docs/WhatsNew.md`) antes de compilar, exige un árbol commiteado y pusheado, y publica en el release `v<versión>` de este repo. La primera plataforma crea el release; la segunda suma sus archivos y fusiona `SHA256SUMS` (una línea por archivo, sin tocar las de la otra plataforma). Al final suben las notas del release y `whats_new.json`. En macOS publica `tools/macos/publish_release.sh` (lo propio de esta app está en `tools/macos/release.conf`): antes de compilar comprueba todo lo que impediría publicar, y con el `.zip` y el `.dmg` armados lo vuelve a comprobar y publica; si la publicación falla, la app se instala igual y el script sale con error. Un release que ya tiene los archivos de la plataforma no se pisa sin `--replace`.

### Capturas de UI sin abrir ventanas

```bash
VideoDownloader.exe --ui-shot <estado> <salida.png> [--dpr 1.5] [--size 1200x860]
```

Estados: `empty`, `downloading`, `error`, `tools`, `other-errors`, `help`, `settings`, `settings-update`, y los carteles del update `update-prompt`, `whats-new` y `update-failed` (con `--notes <whats_new.json>`, que `whats-new` exige). Dibuja la ventana real con datos de prueba y escribe al lado un `.json` con la geometría. `--qa-walkthrough <links.txt> <carpeta> --qa-isolated <destino>` corre la app real sin ventana (plataforma offscreen), pega los links, aprieta Download y guarda capturas. `--qa-titlebar` (Windows, con el runner en `-Platform windows`) prueba el marco propio con la ventana real: estilo nativo, qué responde Windows en la barra, los botones, el cuerpo y los bordes, y maximizar, restaurar, minimizar y cerrar con los botones de la barra.

## Estructura

```
include/videodownloader/   Cabeceras
src/core/                  DownloadQueue (cola y yt-dlp), NativeHost (extension)
src/ui/                    MainWindow, AppHeaderBar (barra de titulo) y windowsframeless (marco propio en Windows),
                           AddVideosCard, QueueView, LogView, HelpDialog y SettingsDialog, tema
src/utils/                 ToolsManager/ToolsUpdater (tools), BrowserDetect, LinkParser
src/qa/                    --ui-shot, --qa-walkthrough, --qa-titlebar y los demas --qa-*
libs/lgaupdate/            Update de la app (biblioteca compartida de las apps LGA)
resources/                 Fuentes Inter, iconos, fondo del DMG
tools/, toolsmac/          yt-dlp, ffmpeg y deno que se distribuyen con la app
```

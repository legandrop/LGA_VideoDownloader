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
- **Help** (icono `?`): versión, actualizaciones, versiones de yt-dlp/ffmpeg/deno y créditos.

Configuración: `%APPDATA%\LGA\VideoDownloader\config.ini` (Windows) o `~/Library/Application Support/LGA/VideoDownloader/config.ini` (macOS).

## Actualizaciones

- **yt-dlp y deno:** al abrir la app se buscan versiones nuevas en GitHub (tag fijo, SHA-256 obligatorio) y se instalan en la carpeta de datos del usuario. El reemplazo se hace solo cuando no hay una descarga corriendo.
- **La app:** se busca el último release de GitHub con su `SHA256SUMS`. Si hay uno nuevo, aparece "Update available" arriba a la derecha y el botón Update en Help. En Windows descarga el instalador verificado e instala sobre la misma carpeta; en macOS abre la página del release.
- **ffmpeg** viene con la app y no se actualiza solo.

Detalle por plataforma: [PLATFORM_DIFFERENCES.md](PLATFORM_DIFFERENCES.md). Cola y parseo de yt-dlp: [DOWNLOAD_QUEUE_SYSTEM.md](DOWNLOAD_QUEUE_SYSTEM.md).

## Compilación

Requisitos: Qt 6.8.2, CMake 3.16+, compilador C++17.

```bash
./compilar.bat      # Windows (siempre este script, no cmake a mano)
./compilar.sh       # macOS
./deploy.bat        # Windows: versión portable
./deploy.sh --zip --dmg   # macOS: .zip de actualización y .dmg de instalación
```

### Capturas de UI sin abrir ventanas

```bash
VideoDownloader.exe --ui-shot <estado> <salida.png> [--dpr 1.5] [--size 1200x860]
```

Estados: `empty`, `downloading`, `error`, `tools`, `other-errors`, `help`, `help-update`, `help-downloading`. Dibuja la ventana real con datos de prueba y escribe al lado un `.json` con la geometría. `--qa-walkthrough <links.txt> <carpeta> --qa-isolated <destino>` corre la app real sin ventana (plataforma offscreen), pega los links, aprieta Download y guarda capturas.

## Estructura

```
include/videodownloader/   Cabeceras
src/core/                  DownloadQueue (cola y yt-dlp), UpdateService (update de la app)
src/ui/                    MainWindow, AddVideosCard, QueueView, LogView, HelpDialog, tema
src/utils/                 ToolsManager/ToolsUpdater (tools), BrowserDetect, LinkParser
src/qa/                    --ui-shot y --qa-walkthrough
resources/                 Fuentes Inter, iconos, fondo del DMG
tools/, toolsmac/          yt-dlp, ffmpeg y deno que se distribuyen con la app
```

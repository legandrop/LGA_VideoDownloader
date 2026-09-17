# Sistema de cola de descargas

Cómo funciona la cola de `DownloadQueue` (`src/core/downloadqueue.cpp`) y cómo la muestra la UI.

## Piezas

- **`DownloadItem`** (`downloaditem.h`): un link con sus `DownloadOptions` (cookies de navegador o `cookies.txt`, carpeta, formato MP4/M4A y calidad Compatible/Best), su estado (`Pending`, `Downloading`, `Completed`, `Failed`, `Cancelled`), los datos que informa yt-dlp (título, extractor, resolución, bytes, velocidad, ETA, archivo final) y, si falló, `failure` + titular y detalle para la tarjeta.
- **`DownloadQueue`**: no toca widgets. Guarda los items en orden, lanza yt-dlp de a uno y avisa con señales: `itemAdded`, `itemUpdated`, `itemRemoved`, `logLine(texto, nivel)` y `videoPasswordRequired`.
- **UI**:
  - `MainWindow` conecta todo.
  - `AddVideosCard` junta los links y las opciones, y `LinkParser` separa los links del texto pegado.
  - `QueueView` pinta una `QueueCard` por item y `LogView` el log.

## Flujo

1. **Download:**
   - `LinkParser::parse` separa el texto por espacios, saltos, comas y punto y coma.
   - Se queda con lo que parece un link: `http(s)://`, un dominio conocido o `www.` sin esquema.
   - Descarta los repetidos y agrega un item por link. Se acepta cualquier sitio.
   - Si no había ningún link, aparece una sola tarjeta "No link found". Si había links y además texto suelto, el log avisa "N items ignored".
2. `processNextDownload` toma el primer `Pending`. Si las tools todavía no están instaladas, espera; `MainWindow` llama `kick()` cuando quedan listas.
3. **`startDownloadProcess` arma los argumentos de yt-dlp:**
   - cookies y `--video-password` si corresponde;
   - `--format bv*+ba/b/ba` (los sitios solo de audio bajan el audio);
   - con Compatible, `--format-sort vcodec:h264,res,acodec:aac`; con Best, `--format-sort res`; en los dos casos mezcla a mp4;
   - con M4A, `--extract-audio --audio-format m4a`;
   - `--match-filter "live_status!=?is_live & live_status!=?is_upcoming"`: yt-dlp saltea los vivos sin lanzar ffmpeg;
   - `--ffmpeg-location`, y `--js-runtimes deno:<ruta>` para YouTube.
4. **Salida estructurada** (se reconoce con `startsWith`, sin regex por línea):
   - `[vdprog] bajados|total|estimado|velocidad|eta`: progreso. Se pondera por bytes con el tamaño anunciado y se emite como máximo cada 150 ms.
   - `[vdinfo] formato|resolución|ext|tamaño|live_status|extractor|título`: datos para la tarjeta. Un `is_live` que pase el filtro se corta acá.
   - `[vdfile] ruta`: archivo final.
   - `[download] Destination:` y `[Merger]`: rutas para borrar parciales al cancelar.
   - stdout y stderr se decodifican con un `QStringDecoder` por stream, así un carácter UTF-8 partido entre lecturas no se rompe.
5. **Al terminar:**
   - Exit 0 → `Completed`.
   - Error de contraseña de video → diálogo y reintento con `--video-password`.
   - Cualquier otro error → `classifyFailure` traduce la salida de yt-dlp: sesión necesaria (por sitio), navegador que no deja leer sus cookies, vivo, DRM, sitio no soportado, video no disponible, red, IP bloqueada o genérico.
   - Un fallo cuenta un solo error en el log.

## Cancelar y cerrar

- **Cancelar la tarjeta en curso** mata el árbol de procesos. Al llegar `finished` se borran solo los parciales de ese item: `<destino>.part`, `.ytdl`, `.part-FragN`, los streams intermedios `<título>.f<formato>.<ext>` (solo si había video + audio separados) y el `.temp` de la unión.
- **Cancelar un item en cola** solo lo marca `Cancelled`.
- **Cancel all, cerrar la app o instalar un update** (`stopAllForShutdown`) hacen lo mismo con la descarga en curso.

## Reintentos

- **Retry** (tarjeta) y **Retry failed** (pie) conservan formato, calidad y carpeta de cada item, y usan la sesión elegida en ese momento en *Use cookies from*.
- **Retry with Firefox** cambia la sesión a Firefox y reintenta.
- No se reintentan: los sitios no soportados, los vivos y "No link found".

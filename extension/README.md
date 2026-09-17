# Extensión de navegador de LGA Video Downloader

Extensión Manifest V3 para Brave, Chrome y Edge. Manda la página actual (o un link, desde el
menú contextual) a la cola de LGA Video Downloader con un clic, opcionalmente con las cookies
del sitio para videos privados o con restricción de edad. Habla con la app por Native
Messaging; no usa internet ni carga nada remoto.

- **ID fijo:** `nccihgcjogpmmlldndbiabgbegmlogjn` (ver `EXTENSION_ID.txt`). Sale del campo
  `key` de `manifest.json`: SHA-256 de la clave pública DER, primeros 32 hex mapeados a `a-p`.
- **Host nativo:** `com.lga.videodownloader`. Vive en un único lugar, `lib/config.js`
  (`HOST_NAME`), y tiene que coincidir con el `name` del JSON del host que instala la app.

## Instalación (modo desarrollador)

1. Abrir `brave://extensions` (Chrome: `chrome://extensions`, Edge: `edge://extensions`).
2. Encender **Developer mode** y dejarlo encendido: si se apaga, el navegador deshabilita la extensión.
3. **Load unpacked** y elegir esta carpeta (la que se instala junto a la app).
4. Fijar el icono en la barra.

La app tiene que estar instalada: registra el host nativo. Sin eso el popup muestra
"LGA Video Downloader is not installed". Después de una actualización de la app, el popup
detecta la versión nueva en disco y recarga la extensión; si no alcanza, botón **Reload** en
la tarjeta de la extensión.

## Protocolo v1

Un mensaje por pedido (`chrome.runtime.sendNativeMessage`), una respuesta.

Extensión → host:

```json
{ "v": 1, "type": "ping" }
{ "v": 1, "type": "download", "url": "https://...", "title": "...", "browser": "brave",
  "cookies": [ { "domain": ".youtube.com", "hostOnly": false, "path": "/", "secure": true,
                 "httpOnly": true, "expirationDate": 1790000000, "name": "SID", "value": "..." } ] }
```

- `url`: solo `http`/`https`, host no vacío, ≤ 4096 caracteres. `title` ≤ 512. `browser` ≤ 32 (solo para el log).
- `cookies` ausente = sin sesión. `expirationDate` ausente = cookie de sesión. Máximo 3000;
  se descartan una por una las que tengan tab o salto de línea en `name`/`value`/`domain`/`path`.
  La app arma el cookies.txt.
- Mensaje completo ≤ 8 MB; si se pasa, no se envía.

Host → extensión:

```json
{ "v": 1, "ok": true, "status": "pong", "running": true, "app": "0.94", "extension": "1.0.0" }
{ "v": 1, "ok": true, "status": "queued", "launched": false, "app": "0.94", "extension": "1.0.0" }
{ "v": 1, "ok": false, "error": "bad_request|unsupported_version|app_start_failed|app_timeout|internal", "message": "..." }
```

La extensión rechaza como inválida cualquier respuesta con otro `v`, un `status` inesperado o
un `ok` que no sea booleano. `extension` es la versión del `manifest.json` instalado junto a la
app y dispara la recarga si difiere.

## Privacidad y seguridad

- Las cookies se leen **solo** al apretar Download o el item del menú contextual, nunca al abrir
  el popup. Se toman las del dominio registrable de la URL (con alias `youtu.be → youtube.com`,
  `x.com ↔ twitter.com`), sin cookies particionadas.
- "Use my session" es un único interruptor global, encendido por defecto. Lo único que se guarda
  en `chrome.storage.local` es ese booleano, la última versión recargada y el último fallo del
  menú contextual. Nunca cookies.
- Sin content scripts, sin `scripting`, sin `externally_connectable` ni mensajería externa, sin
  código remoto y con CSP estricta (`default-src 'none'`, todo `'self'`). Deshabilitada en incógnito.
- Permisos: `nativeMessaging`, `cookies`, `contextMenus`, `storage` y `<all_urls>`, que la cookies
  API exige para leer las cookies del sitio. El navegador lo muestra como acceso a todos los sitios.
- La clave privada de la `key` no está en el repo y no hace falta para cargar la extensión unpacked.

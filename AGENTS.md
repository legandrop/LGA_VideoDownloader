# Instrucciones para LGA VideoDownloader

- Todas las reglas de este repo (`AGENTS.md`, `CLAUDE.md`, `.cursor/rules/`) deben estar escritas en castellano.
- Este archivo es uno de **tres espejos** del mismo contenido:
  - `CLAUDE.md` (Claude Code)
  - `AGENTS.md` (Codex)
  - `.cursor/rules/instructions.mdc` (Cursor)
- Al modificar cualquiera de los tres, **correr `./sync_rules.sh`** (Windows: `sync_rules.bat`) y listo: copia el contenido a los otros dos y le pone el frontmatter al `.mdc`. Es lo unico que puede diferir entre ellos (`alwaysApply: true`).
- ✅ **HOOK: corriendo en mac y windows.** Hay un `pre-commit` que corta el commit si los tres espejos no coinciden. Se activa una sola vez **por maquina y por repo** con `sync_rules.bat --install-hook` (mac: `./sync_rules.sh --install-hook`), porque `core.hooksPath` es config local y no viaja en el clon: si se formatea una maquina o se re-clona el repo, hay que volver a activarlo ahi. Lo mismo con `git config blame.ignoreRevsFile .git-blame-ignore-revs`.
- **El interprete de Python lo eligen el hook y los wrappers PROBANDO cada candidato, no con `command -v`.** Orden: el `python_runtime/` del propio repo, el de `../LGA_FileManagerS3`, `py -3`, y recien despues el `python3`/`python` del PATH. En Windows esos dos ultimos existen en el PATH como alias del Microsoft Store: estan, imprimen "Python was not found" y salen con error, y por eso el hook nunca habia podido correr en Windows. Si no hay ninguno, el hook avisa y **deja pasar** el commit: bloquear por una dependencia faltante es peor que no verificar. La idea de fondo es no instalar un Python global, que taparia el dia que una app portable deja de andar con su runtime embebido.

## Que es esta app

Descarga videos de **cualquier sitio que soporte `yt-dlp`** (YouTube, Vimeo, SoundCloud, Instagram, TikTok...), con cola de descargas y sesion tomada de las cookies del navegador (nunca usuario/contrasena). Qt/C++ multiplataforma. Los sitios conocidos tienen nombre propio en la tarjeta (`LinkParser::siteName`); el resto muestra el extractor de yt-dlp o "Other site".

**Importante:** "Vimeo" aparece en el repo en DOS roles distintos y no son intercambiables:

- **El servicio**: `vimeo.com` como sitio (`DownloadItem::isVimeo()`, `LinkParser`, textos de error de sesion, la migracion del AppData `VimeoDownloader` en `main.cpp`). Eso es funcional y NO se renombra.
- **El nombre del producto**: se renombra cuando corresponda.

Antes de cualquier barrido de nombres, separar los dos.

## Iconos de macOS

**El icono se disena en Icon Composer, NO se genera.** El usuario exporta un `.icon` a `resources/icons/Alta/<Nombre>.icon` (es un directorio: `icon.json` + `Assets/`), y de ahi se compila con `actool`.

**📄 Instrucciones completas y actualizadas: seccion "5.2 Icon Composer" de `../LGA_IconLab/docs/Doc_Iconos_App.md`.** Ahi estan los comandos exactos, la tabla de tamanos medida, el cableado de CMake y como verificar. El flujo completo (cuando corresponde placeholder, quien aprueba, como se aplica) esta en las reglas de `../LGA_IconLab` — secciones "Flujo vigente", "Protocolo para probar un `.icon`" y "Aplicar un `.icon` aprobado a la app".

Lo que mas cuesta si no se lee:

- **Siempre `--standalone-icon-behavior all`**: sin ese flag el `.icns` auxiliar trae 4 representaciones y llega hasta 256 px; con el son 10 y llega a 1024.
- **Copiar el `.icon` a `AppIcon.icon` antes de compilar**: `actool` nombra el asset segun el archivo, y de ahi sale el valor de `CFBundleIconName`.
- **Van DOS recursos al bundle**: `Assets.car` (el icono real, via `CFBundleIconName`) y `AppIcon.icns` (fallback, via `CFBundleIconFile`). **NO** descartar el `Assets.car` quedandose solo con el `.icns`: eso pierde el comportamiento moderno y reaparece el problema de tamano en Cmd+Tab.
- **Verificar la fecha del `.icon`** antes de compilarlo: se re-exportan con el mismo nombre y es facil probar el viejo.
- **Un build incremental NO borra** los recursos que el `CMakeLists` dejo de copiar: al cambiar de icono, revisar que no quede el `.icns` viejo adentro del bundle.
- **El cache muerde**: el bundle id de una app real es fijo, asi que puede seguir mostrando el icono viejo. `touch <App>.app` + `lsregister -f`.
- **Windows es independiente** y no se toca: su `.ico` sale del pipeline generativo de `../LGA_IconLab` (glyph sin fondo).

## Build

- **Compilar es algo a evaluar en cada cambio**, no automatico. Compilar cuando el usuario lo pide, cuando el cambio toca C++ de forma no trivial, cuando se agregan o quitan archivos del build, o cuando no alcanza con leer el codigo para saber si funciona.
- No compilar para cambios que no pueden romper el build: documentacion, changelog, comentarios, reglas del repo.
- Al compilar, usar SIEMPRE el script del repo — NUNCA `cmake`, `ninja` o `make` a mano:
  - Windows: `./compilar.bat`
  - macOS: `./compilar.sh`
- No hacer builds limpios automaticamente. No borrar `build/` salvo pedido explicito.
- Si la compilacion falla, corregir el problema SIN limpiar primero.
- **Capturas de QA sin escritorio**: `VideoDownloader.exe --ui-shot <estado> <out.png> [--dpr N] [--size WxH]` construye la ventana real con datos de prueba (estados `empty`, `downloading`, `error`, `tools`, `other-errors` —texto sin links, vivo, cancelada, chips de sitio y sitio no soportado—, `help`, `help-update`, `help-downloading`), la dibuja a un PNG nuevo sin mostrarla ni tomar el foco y deja al lado un `<out.png>.json` con la geometria de los widgets y las fuentes resueltas.
- `--qa-walkthrough <links.txt> <carpeta-existente> [--qa-isolated <carpeta-descargas>]` corre la app completa con `QT_QPA_PLATFORM=offscreen`: pega los links, aprieta Download y guarda capturas numeradas hasta que la cola termina, mas `items.json` y `log.txt`. Hace descargas reales; `--qa-isolated` usa config y tools de prueba en vez de los del usuario (las tools salen de la carpeta del exe). Con `--qa-isolated` tambien: `--qa-setting clave=valor` (repetible, p.ej. `download/quality=best`) escribe esa clave en la config de prueba, y `--qa-cancel-at N` aprieta el Cancel real de la tarjeta cuando pasa el N% (se revisa cada 2,5 s, asi que puede pasarse). `--qa-parse <texto.txt>` lista los links que extrae `LinkParser`, sin red.
- **Firewall de la maquina de Lega (NetLimiter):** solo pueden salir a internet ejecutables en `C:\Portable\LGA\VideoDownloader\**`, `%LOCALAPPDATA%\LGA\VideoDownloader\tools\**` y `C:\Portable\LGA_VideoDownloader\build\**`. No copiar yt-dlp/ffmpeg/deno a otras rutas para probar: el proceso queda colgado y no se puede matar.

## Herramientas de terceros

- `tools/` (Windows) y `toolsmac/` (macOS) contienen **ffmpeg, yt-dlp y deno**, que se distribuyen con la app.
- **No editarlos, no renombrarlos, no renormalizarlos.** Estan marcados `-text` en `.gitattributes`.
- Los de `toolsmac/` **no tienen extension** (`deno`, `ffmpeg`, `yt-dlp`), asi que ninguna regla por extension los protege: la exclusion es por directorio.
- Los binarios de `yt-dlp` contienen la palabra `vimeo` en sus extractores internos. Es normal y no se toca.

## Versionado y changelog

- `CMakeLists.txt` es la **unica fuente de verdad** del numero de version, via `project(VideoDownloader VERSION x.y ...)`.
- El archivo `VERSION` es un espejo **DERIVADO**, no la fuente: existe para que los scripts de shell (deploy, DMG, instalador) lean una linea en vez de parsear CMake, y para que este repo tenga la misma forma que el resto de las apps LGA. Lo escribe `./sync_version.sh`; **nunca se edita a mano**. Si `VERSION` y `CMakeLists.txt` discrepan, el que esta mal es `VERSION` (`./sync_version.sh --check-only` lo detecta).
- El codigo C++ usa la macro `VIDEODOWNLOADER_VERSION`, **nunca un literal**. Lo mismo el `Info.plist`: se deriva de `PROJECT_VERSION` via `MACOSX_BUNDLE_BUNDLE_VERSION` / `MACOSX_BUNDLE_SHORT_VERSION_STRING`.
- **Los tres lugares por donde se escapa el numero** (los tres estuvieron mal en este repo): el `cmake/Info.plist.in`, el heredoc de `deploy.sh` que reescribe el plist entero al deployar, y los `setWindowTitle()`. Al bumpear, revisarlos.
- Changelog principal: `docs/ChangeLog.md`.
- El numero al comienzo del changelog es la version mas alta registrada. Si el changelog ya esta por encima de `CMakeLists.txt`, **no subir version**: agregar la entrada nueva arriba de las existentes dentro de esa version.
- La entrada nueva va inmediatamente debajo del numero de version, con una linea en blanco debajo.
- Siempre en castellano.
- Al final de cada entrada, entre `[ ]`, una sugerencia de nombre corto para el commit. Ejemplo: `[ Iconos - Adoptar el diseno v003 ]`
- **Nunca reescribir ni modificar una entrada existente.** Si el cambio evoluciona o se corrige, agregar otra entrada nueva arriba con su propio nombre entre `[ ]`.
- Longitud (regla blanda): ~100-150 palabras. Ir al grano: que se rompio + que causa + que se cambio.

## Deploy de macOS

- `./deploy.sh --zip --dmg` produce los dos artefactos: el `.zip` (actualizacion in-place) y el `.dmg` (primera instalacion). No son intercambiables.
- **Fuente de verdad: `../LGA_Base_QT_C_Py/docs/Doc_Deploy_macOS.md`** — bundle autocontenido, firma ad-hoc antes de empaquetar, el `.zip` con `ditto` y nunca con `zip`, y las restricciones de Finder que definen el DMG dark.
- Son TRES nombres y no hay que confundirlos: el `.app` y el ejecutable se llaman **`LGA Video Downloader`** (con espacios — es lo que ve el usuario en `/Applications`, donde Finder ordena por el nombre de ARCHIVO del bundle y no por `CFBundleName`); los artefactos `.zip`/`.dmg` se llaman **`LGA_Video_Downloader_Mac_v<version>`** (SIN espacios, porque viajan por URL en los releases y un espacio se vuelve `%20`); y `DISPLAY_NAME` —volumen del DMG y titulo del fondo— dice lo mismo que el `.app`. Las tres salen de constantes arriba de `create_dmg.sh`.
- El rename va SOLO en el bloque `if(APPLE)` del CMakeLists: en Windows el ejecutable y el instalador siguen siendo `VideoDownloader`.
- **Esta app TIENE auto-updater** (`UpdateService`): lee el tag del redirect de `github.com/legandrop/LGA_VideoDownloader/releases/latest` (sin API) y del MISMO release baja `SHA256SUMS` y el asset `VideoDownloader_Setup_v<version>.exe` (Windows) o `LGA_Video_Downloader_Mac_v<version>.zip` (macOS, por ahora solo abre la pagina del release). Cambiar el nombre de esos artefactos rompe la actualizacion de las copias instaladas.
- **Todo release lleva `SHA256SUMS`** (formato `hash  nombre`) con las lineas de TODOS los assets de update: sin la linea de su asset, la app no ofrece el update. `instalador.bat` genera `installer/SHA256SUMS` y `deploy.sh` genera `deploy/SHA256SUMS`; si se publican las dos plataformas en el mismo release, se suben concatenados en un solo `SHA256SUMS`.
- **No hay que instalar nada**: `dmgbuild` va vendorizado en `tools/macos/vendor/` (Python puro, corre con el `python3` del sistema) y el fondo lo genera `tools/macos/make_dmg_background.js` con AppKit via JXA. El `.tiff` esta versionado en `resources/dmg/`; regenerarlo solo hace falta si cambia el diseno o el nombre.

## Tools en runtime (yt-dlp y deno)

- Se instalan y actualizan solas al abrir la app (`ToolsUpdater`), desde GitHub con tag fijo y SHA-256 obligatorio, en `%LOCALAPPDATA%\LGA\VideoDownloader\tools` (macOS `~/Library/Application Support/LGA/VideoDownloader/tools`). Nunca se escriben dentro de la instalacion ni del bundle.
- Orden de resolucion: carpeta de usuario → copia de la instalacion (`tools/`, `toolsmac/`) → PATH en macOS.
- El swap de un binario verificado se hace en UN solo punto: al arrancar y antes de lanzar cada proceso de yt-dlp (`ToolsManager::applyStagedTools`).
- Para probar sin publicar: `LGA_VD_GITHUB_BASE` (tools y app), que solo acepta `localhost`/`127.0.0.1`.

## Commits

- **NUNCA hacer commits automaticamente.** Commitear solo cuando el usuario lo pide explicitamente o cuando es parte de un plan que ya aprobo.
- Todo commit o push se hace con la identidad de Git del usuario `legandrop`. Sin coautores, autores alternativos ni footers de atribucion.
- **La cuenta correcta es `legandrop`, ID de GitHub `176236735`**, o sea el email `176236735+legandrop@users.noreply.github.com`. **Verificar `git config user.email` ANTES de commitear**: si no es ese, corregirlo y avisar antes de seguir. Ya pasó dos veces que quedaran commits con `89184004+legandrop@...`, una cuenta vieja: el nombre se ve igual, pero GitHub atribuye por email, así que esos commits quedan fuera del gráfico de contribuciones de la cuenta buena. Los que ya están en la historia **no se reescriben**.
- **No mencionar herramientas, agentes, modelos de IA ni asistentes** en commits, changelog, README, documentacion, PRs, issues, releases ni comentarios de codigo. El texto describe el cambio del PROYECTO en sus propios terminos, no el entorno de quien lo hizo.

## Line endings

- El repo esta normalizado a **LF**, con **CRLF solo en `.bat`, `.cmd` y `.ps1`** (los necesitan para que `cmd.exe` los interprete bien). La politica vive en `.gitattributes` y `.editorconfig`; no hay que decidir nada por archivo.
- `tools/` y `toolsmac/` estan marcados `-text`: son binarios de terceros y NO se renormalizan.
- Al editar, **cambiar solo las lineas necesarias**. No reescribir un archivo entero ni normalizar line endings como efecto colateral de un cambio no relacionado.
- Si un archivo aparece modificado ENTERO en el diff, parar y averiguar si hubo conversion de line endings antes de seguir. `git diff --ignore-cr-at-eol` muestra el cambio real de contenido.
- No mezclar renormalizacion con cambios funcionales: va en su propio commit, y se agrega al `.git-blame-ignore-revs`.

## Politica de idioma

Convencion LGA cross-app:

- **Texto visible en UI** (labels, botones, titulos de ventana, mensajes al usuario): siempre en **INGLES**.
- **Comentarios de codigo y mensajes de log de debug**: siempre en **CASTELLANO**.
- **Strings de error internas** que no se muestran al usuario final: pueden estar en castellano.

#ifndef UISHOT_H
#define UISHOT_H

#include <QStringList>

// `--ui-shot <estado> <salida.png> [--dpr N]`: dibuja la ventana real con datos de prueba a
// un PNG, sin mostrar ventanas. Devuelve el codigo de salida del proceso.
int runUiShot(const QStringList &args);

// `--qa-walkthrough <links.txt> <carpeta>`: la app real con plataforma offscreen pega los
// links, aprieta Download y guarda capturas hasta que la cola termina. Usa los settings del
// usuario (carpeta de descarga incluida) y descarga de verdad.
int runWalkthrough(const QStringList &args);

// `--qa-parse <texto.txt>`: lista los links que LinkParser extrae de un texto (sin red).
int runParseCheck(const QStringList &args);

// `--qa-migrate <carpeta-origen> <carpeta-destino>`: corre la migracion de tools de AppPaths
// entre dos carpetas cualquiera, para probarla sin tocar las del usuario. Sin red. Imprime lo
// que movio y como quedaron las dos carpetas.
int runMigrateCheck(const QStringList &args);

// `--qa-update-dirs <carpeta-app> <carpeta-fallback>`: sin red y sin lanzar nada, resuelve la
// carpeta del instalador del auto-update como lo hace la app (`<carpeta-app>/updates` si acepta
// una escritura real; si no, el fallback) y corre el barrido de instaladores viejos sobre esas
// carpetas y la de %TEMP% del proceso (TMP/TEMP). Imprime lo que resolvio y lo que quedo.
int runUpdateDirsCheck(const QStringList &args);

// `--qa-tools-check <carpeta>`: sin red y SIN ejecutar nada, dice que haria la app con las
// tools de esa carpeta: por cada .exe y .dll, si su cabecera es valida, y si yt-dlp, deno y
// ffmpeg (con ffprobe y sus .dll) se lanzarian o se tratarian como rotos. Usa las mismas
// funciones que consulta la app antes de lanzar. Para probar con archivos senuelo en una
// carpeta de prueba, nunca en la carpeta de tools de una copia de la app.
int runToolsCheck(const QStringList &args);

// `--qa-cookies`: test sin red del armado de cookies.txt y de la validacion del protocolo de
// la extension. Imprime PASS/FAIL por caso y devuelve 0 si pasan todos.
int runCookiesCheck();

// `--qa-whats-new [<whats_new.json> [<instalada> <ofrecida>]]`: test sin red de las notas (regla
// de version, "despues de instalar", parse estricto, rango y escape). Con un archivo, imprime
// el rango por plataforma. Devuelve 0 si pasan todos.
int runWhatsNewCheck(const QStringList &args);

// `--qa-whats-new-fetch <carpeta-cache> [--qa-close-after-ms N]`: con LGA_VD_GITHUB_BASE en un
// servidor local, corre el chequeo real de update y la descarga de notas, con la cache en esa
// carpeta. Con --qa-close-after-ms destruye el servicio N ms despues de ofrecer el update, con
// la descarga de notas todavia en curso (el servidor tiene que demorarla).
int runWhatsNewFetch(const QStringList &args);

#endif // UISHOT_H

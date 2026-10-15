#ifndef UISHOT_H
#define UISHOT_H

#include <QStringList>

class QWidget;

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
// una escritura real; si no, el fallback) y corre el barrido del arranque sobre ella (lgaupdate
// la vacia entera) y sobre la de %TEMP% de las versiones <= 0.95 (TMP/TEMP del proceso). Imprime
// lo que resolvio y lo que quedo.
int runUpdateDirsCheck(const QStringList &args);

// `--qa-tools-check <carpeta>`: sin red y SIN ejecutar nada, dice que haria la app con las
// tools de esa carpeta: por cada .exe y .dll, si su cabecera es valida, invalida o no se pudo
// leer (`valid` / `INVALID` / `UNVERIFIABLE`: lo que no se puede leer no cuenta como roto), y
// si yt-dlp, deno y ffmpeg (con ffprobe y sus .dll) se lanzarian. Usa las mismas
// funciones que consulta la app antes de lanzar. Para probar con archivos senuelo en una
// carpeta de prueba, nunca en la carpeta de tools de una copia de la app.
int runToolsCheck(const QStringList &args);

// `--qa-cookies`: test sin red del armado de cookies.txt y de la validacion del protocolo de
// la extension. Imprime PASS/FAIL por caso y devuelve 0 si pasan todos.
int runCookiesCheck();

// Prueba del update de punta a punta contra un servidor local: con LGA_VD_GITHUB_BASE en
// localhost y LGA_VD_QA_ACCEPT_UPDATE=1, aprieta "Update now" en cuanto aparece el cartel de version
// nueva, como lo haria el usuario. Sin las dos variables no hace nada. La app relanzada por el
// update hereda el entorno, pero ya no tiene version nueva que ofrecer.
void installQaUpdateAutoAccept(QWidget *owner);

#endif // UISHOT_H

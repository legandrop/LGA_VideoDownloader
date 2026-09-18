#ifndef APPPATHS_H
#define APPPATHS_H

#include <QString>
#include <QStringList>

// Donde viven los datos pesados que escribe la app en runtime (tools auto-actualizadas y
// cookies temporales de la extension).
//
// Regla de las apps LGA:
// - Windows: todo lo pesado DENTRO de la carpeta de la app (`<carpeta del exe>/<nombre>`).
//   Si esa carpeta no es escribible (p.ej. instalada en Program Files) se cae a
//   `%LOCALAPPDATA%/LGA/VideoDownloader/<nombre>`, detectado con un intento real de escritura.
// - macOS: FUERA del bundle, en `~/Library/Application Support/LGA/VideoDownloader/<nombre>`,
//   porque escribir dentro del .app rompe la firma. No cambia.
// - AppData (Roaming) queda solo para settings chicos.
namespace AppPaths {

// Carpeta para `name` ("tools", "session-cookies", "updates"). No la crea salvo para probar escritura
// en Windows. El resultado se resuelve una vez por nombre y queda fijo durante el proceso.
QString heavyDataDir(const QString &name);

// `%LOCALAPPDATA%/LGA/VideoDownloader/<name>` (macOS: Application Support). Es la ubicacion
// del diseno anterior en Windows y la vigente en macOS.
QString userDataDir(const QString &name);

// Nucleo de heavyDataDir(), sin cache y con las dos carpetas explicitas: `inAppDir` si acepta
// una escritura real (sin crearla: se prueba en el padre si todavia no existe), y si no
// `fallbackDir`. Fuera de Windows devuelve siempre `fallbackDir`. Lo usa `--qa-update-dirs`
// para probar la eleccion con carpetas de prueba.
QString chooseHeavyDataDir(const QString &inAppDir, const QString &fallbackDir, bool *usedFallback = nullptr);

// true si en Windows la carpeta de `name` tuvo que caer a LOCALAPPDATA.
bool usesFallback(const QString &name);

// true si `dir` tiene pinta de carpeta de tools: `yt-dlp.exe`, `deno.exe` o `tools.json`
// (sin `.exe` en macOS), sueltos o en `.staging`. Guarda contra borrar una carpeta ajena.
bool looksLikeToolsFolder(const QString &dir);

// Migra una carpeta de tools a otra (Windows). Mueve lo que este en staging verificado y las
// tools instaladas que sean MAS NUEVAS que las del destino, decidiendo por el `--version` del
// binario y no por tools.json. Solo borra la carpeta de origen si no quedo NADA atras. Es el
// motor de migrateLegacyWindowsData() y lo usa `--qa-migrate` para probarlo con carpetas
// cualquiera. Devuelve lineas para el log visible.
QStringList migrateToolsFolder(const QString &legacyDir, const QString &targetDir);

// Windows: mueve a `<app>/tools` las tools de LOCALAPPDATA que sean mas nuevas y borra la
// carpeta vieja entera (y `session-cookies` si quedo vacia). Si algo no se puede mover o
// borrar, queda para el proximo arranque. En macOS no hace nada. Devuelve lineas para el log
// visible (vacio si no hubo nada que hacer).
QStringList migrateLegacyWindowsData();

} // namespace AppPaths

#endif // APPPATHS_H

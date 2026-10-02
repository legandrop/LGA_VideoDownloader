#ifndef WHATSNEW_H
#define WHATSNEW_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// Notas de version para el usuario ("What's new"), leidas del asset `whats_new.json` que lleva
// cada release. Solo la parte pura: parse, rango de versiones, filtro de plataforma y HTML. La
// descarga la hace UpdateService.
//
// Formato del archivo:
//
//     {"schemaVersion": 1, "product": "legandrop/LGA_VideoDownloader", "name": "...",
//      "versions": [{"version": "0.96", "date": "2026-10-02",
//                    "items": [{"kind": "new", "text": "...", "platform": ["win"]}]}]}
//
// `kind` es new, improved o fixed. `platform` (win, mac) ausente = todas. `for` y `edition`
// pueden venir como listas de textos: esta app no tiene ediciones ni roles y no los usa.
namespace WhatsNew {

inline constexpr const char *kAssetName = "whats_new.json";
inline constexpr int kSchemaVersion = 1;
// Tope del archivo: el historial completo son decenas de KB.
inline constexpr qint64 kMaxBytes = 2 * 1024 * 1024;

enum class Kind { New, Improved, Fixed };

struct Item {
    Kind kind = Kind::New;
    QString text;
    QStringList platforms;  // vacio = todas
};

struct VersionNotes {
    QString version;
    QString date;  // AAAA-MM-DD, puede venir vacia
    QList<Item> items;
};

struct Notes {
    bool valid = false;
    QString error;  // por que no es valido, para el log
    QList<VersionNotes> versions;  // la mas nueva primero
};

// Parse ESTRICTO: JSON roto, otro schemaVersion, otro `product`, una version que no es numerica,
// un item sin texto o con un kind desconocido invalidan TODO. Notas a medias serian peores que
// ninguna, y "sin notas" deja el update exactamente como antes.
Notes parse(const QByteArray &json, const QString &expectedProduct);

// Regla de version de las apps LGA: el minor es un decimal de 3 cifras rellenado a la derecha
// (`0.96` es `0.960`, `2.66 < 2.7`) y lo que sigue al minor son enteros sin ceros finales
// (`0.910.0 == 0.910`). Acepta el prefijo `v`. `okOut` false si alguna no es numerica.
int compareVersions(const QString &left, const QString &right, bool *okOut = nullptr);
bool isValidVersion(const QString &version);

// "win" o "mac".
QString currentPlatform();

// Versiones en (`installedExclusive`, `offeredInclusive`], con los items de `platform`, de la
// mas nueva a la mas vieja; las que quedan sin items se omiten. Un extremo vacio no corta; uno
// que no es una version devuelve nada.
QList<VersionNotes> range(const Notes &notes, const QString &installedExclusive,
                          const QString &offeredInclusive, const QString &platform);

// HTML para un QTextBrowser: numero y fecha de cada version y sus items agrupados en NEW,
// IMPROVED y FIXED, cada rotulo con su color. Todo el texto del archivo va escapado.
QString toHtml(const QList<VersionNotes> &versions);

// Que hacer al arrancar con las notas "despues de instalar", segun la ultima version cuyas notas
// vio el usuario (`updates/whats_new_last_seen`) y la que corre.
enum class AfterInstall {
    Nothing,   // ya las vio (o la que corre es mas vieja)
    SaveOnly,  // instalacion nueva o valor ilegible: se guarda la actual sin mostrar nada
    Show,      // se actualizo sin verlas: se muestra (lastSeen, actual] una vez
};
AfterInstall afterInstallAction(const QString &lastSeen, const QString &current);

} // namespace WhatsNew

#endif // WHATSNEW_H

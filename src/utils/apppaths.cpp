#include "videodownloader/apppaths.h"
#include "videodownloader/toolsupdater.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QVersionNumber>

namespace {

struct Resolved
{
    QString path;
    bool fallback = false;
};

QMutex s_mutex;
QHash<QString, Resolved> s_cache;

#ifdef Q_OS_WIN
// Prueba real de escritura: crear la carpeta y un archivo adentro. No alcanza con mirar la ruta
// (Program Files) ni los permisos de Qt, que en Windows no reflejan las ACL.
bool canWriteInto(const QString &dir)
{
    if (!QDir().mkpath(dir)) {
        return false;
    }
    QTemporaryFile probe(QDir(dir).filePath(QStringLiteral(".write-test-XXXXXX")));
    if (!probe.open()) {
        return false;
    }
    const bool written = probe.write("ok", 2) == 2 && probe.flush();
    probe.close();
    return written; // QTemporaryFile lo borra al destruirse
}

QJsonObject readInstalled(const QString &toolsDir)
{
    QFile file(QDir(toolsDir).filePath(QStringLiteral("tools.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.read(1024 * 1024));
    return doc.object().value(QStringLiteral("installed")).toObject();
}

bool writeInstalledEntry(const QString &toolsDir, const QString &key, const QJsonObject &entry)
{
    const QString path = QDir(toolsDir).filePath(QStringLiteral("tools.json"));
    QJsonObject state;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly)) {
        state = QJsonDocument::fromJson(in.read(1024 * 1024)).object();
        in.close();
    }
    QJsonObject installed = state.value(QStringLiteral("installed")).toObject();
    installed.insert(key, entry);
    state.insert(QStringLiteral("installed"), installed);
    if (!state.contains(QStringLiteral("staged"))) {
        state.insert(QStringLiteral("staged"), QJsonObject());
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray bytes = QJsonDocument(state).toJson(QJsonDocument::Indented);
    return out.write(bytes) == bytes.size() && out.commit();
}

// Version de un binario sin entrada en tools.json (la copia que dejo el instalador): se le
// pregunta `--version` y se toma el primer numero con puntos ("2026.08.19", "deno 2.9.7 ...").
QString probeVersion(const QString &binary)
{
    QProcess process;
    process.start(binary, {QStringLiteral("--version")});
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(2000);
        return QString();
    }
    static const QRegularExpression re(QStringLiteral("(\\d+(?:\\.\\d+)+)"));
    const QRegularExpressionMatch match = re.match(QString::fromUtf8(process.readAllStandardOutput()));
    return match.hasMatch() ? match.captured(1) : QString();
}

// Mueve un archivo dentro del mismo volumen (rename) o, si no se puede, copia y borra.
bool moveFile(const QString &from, const QString &to)
{
    if (QFile::rename(from, to)) {
        return true;
    }
    if (!QFile::copy(from, to)) {
        return false;
    }
    if (!QFile::remove(from)) {
        QFile::remove(to);
        return false;
    }
    return true;
}
#endif

} // namespace

namespace AppPaths {

QString userDataDir(const QString &name)
{
    // GenericDataLocation: %LOCALAPPDATA% en Windows, ~/Library/Application Support en macOS.
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/LGA/VideoDownloader/") + name;
}

QString heavyDataDir(const QString &name)
{
    QMutexLocker lock(&s_mutex);
    const auto it = s_cache.constFind(name);
    if (it != s_cache.constEnd()) {
        return it->path;
    }

    Resolved resolved;
#ifdef Q_OS_WIN
    // Windows: dentro de la carpeta de la app, salvo que no se pueda escribir ahi.
    const QString inApp = QCoreApplication::applicationDirPath() + QLatin1Char('/') + name;
    if (canWriteInto(inApp)) {
        resolved.path = inApp;
    } else {
        resolved.path = userDataDir(name);
        resolved.fallback = true;
        qWarning() << "[AppPaths] La carpeta de la app no es escribible, se usa" << resolved.path
                   << "en vez de" << inApp;
    }
#else
    // macOS: fuera del bundle (escribir dentro del .app rompe la firma).
    resolved.path = userDataDir(name);
#endif
    s_cache.insert(name, resolved);
    return resolved.path;
}

bool usesFallback(const QString &name)
{
    heavyDataDir(name);
    QMutexLocker lock(&s_mutex);
    return s_cache.value(name).fallback;
}

QStringList migrateLegacyWindowsData()
{
    QStringList lines;
#ifdef Q_OS_WIN
    const QString legacyTools = userDataDir(QStringLiteral("tools"));
    const QString legacyCookies = userDataDir(QStringLiteral("session-cookies"));
    const QString targetTools = heavyDataDir(QStringLiteral("tools"));

    // Con la carpeta de la app no escribible, LOCALAPPDATA ES la ubicacion vigente: no se toca.
    if (!usesFallback(QStringLiteral("tools")) && QDir(legacyTools).exists()) {
        const QJsonObject legacyInstalled = readInstalled(legacyTools);
        const QJsonObject targetInstalled = readInstalled(targetTools);
        bool allMoved = true;

        for (ToolsUpdater::Tool tool : {ToolsUpdater::Tool::YtDlp, ToolsUpdater::Tool::Deno}) {
            const QString key = ToolsUpdater::toolKey(tool);
            const QString legacyBin = QDir(legacyTools).filePath(ToolsUpdater::binaryName(tool));
            const QString targetBin = QDir(targetTools).filePath(ToolsUpdater::binaryName(tool));
            if (!QFileInfo(legacyBin).isFile()) {
                continue;
            }
            const QJsonObject legacyEntry = legacyInstalled.value(key).toObject();
            const QVersionNumber legacyVersion =
                QVersionNumber::fromString(legacyEntry.value(QStringLiteral("version")).toString());
            if (legacyVersion.isNull()) {
                qDebug() << "[AppPaths] Migracion:" << key << "sin version registrada, se descarta";
                continue;
            }

            QVersionNumber targetVersion;
            if (QFileInfo(targetBin).isFile()) {
                QString text = targetInstalled.value(key).toObject().value(QStringLiteral("version")).toString();
                if (text.isEmpty()) {
                    text = probeVersion(targetBin);
                }
                targetVersion = QVersionNumber::fromString(text);
                // Sin version legible del destino no se pisa nada: gana el binario de la app.
                if (targetVersion.isNull() || QVersionNumber::compare(legacyVersion, targetVersion) <= 0) {
                    qDebug() << "[AppPaths] Migracion:" << key << "de la app" << text
                             << "no es mas vieja que la de LOCALAPPDATA" << legacyVersion.toString();
                    continue;
                }
            }

            // El binario actual se aparta a .old (cleanupLeftovers lo borra) y entra el nuevo.
            const QString oldPath = targetBin + QStringLiteral(".old.migrated");
            const bool hadTarget = QFileInfo::exists(targetBin);
            QFile::remove(oldPath);
            if (hadTarget && !QFile::rename(targetBin, oldPath)) {
                qWarning() << "[AppPaths] Migracion: no se pudo apartar" << targetBin;
                allMoved = false;
                continue;
            }
            if (!moveFile(legacyBin, targetBin)) {
                if (hadTarget) {
                    QFile::rename(oldPath, targetBin);
                }
                qWarning() << "[AppPaths] Migracion: no se pudo mover" << legacyBin << "(se reintenta)";
                allMoved = false;
                continue;
            }
            QFile::remove(oldPath);
            if (!writeInstalledEntry(targetTools, key, legacyEntry)) {
                qWarning() << "[AppPaths] Migracion: no se pudo registrar" << key << "en tools.json";
            }
            lines.append(QStringLiteral("%1 %2 moved into the app folder").arg(key, legacyVersion.toString()));
            qInfo() << "[AppPaths] Migracion:" << key << legacyEntry.value(QStringLiteral("version")).toString()
                    << "->" << targetBin;
        }

        // Solo se borra la carpeta vieja si todo lo que habia que mover se movio: si no, el
        // proximo arranque vuelve a intentarlo con la informacion de tools.json intacta.
        if (allMoved) {
            if (QDir(legacyTools).removeRecursively()) {
                qInfo() << "[AppPaths] Carpeta de tools anterior borrada:" << legacyTools;
            } else {
                qWarning() << "[AppPaths] No se pudo borrar entera" << legacyTools << "(se reintenta)";
            }
        }
    }

    if (!usesFallback(QStringLiteral("session-cookies")) && QDir(legacyCookies).exists()) {
        // rmdir solo borra la carpeta vacia: cookies de otra copia en uso no se tocan.
        QDir().rmdir(legacyCookies);
    }
#endif
    return lines;
}

} // namespace AppPaths

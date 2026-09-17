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
// Prueba real de escritura, SIN crear la carpeta destino: resolver una ruta no debe dejar
// directorios atras. Si `dir` todavia no existe se prueba en el padre, que es donde habria
// que crearla. No alcanza con mirar la ruta (Program Files) ni los permisos de Qt, que en
// Windows no reflejan las ACL.
bool canWriteInto(const QString &dir)
{
    const QString probeDir = QFileInfo(dir).isDir() ? dir : QFileInfo(dir).absolutePath();
    if (!QFileInfo(probeDir).isDir()) {
        return false;
    }
    QTemporaryFile probe(QDir(probeDir).filePath(QStringLiteral(".write-test-XXXXXX")));
    if (!probe.open()) {
        return false;
    }
    const bool written = probe.write("ok", 2) == 2 && probe.flush();
    probe.close();
    return written; // QTemporaryFile lo borra al destruirse
}

QJsonObject readState(const QString &toolsDir)
{
    QFile file(QDir(toolsDir).filePath(QStringLiteral("tools.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    // Un tools.json roto o truncado devuelve un objeto vacio: se decide por el binario.
    return QJsonDocument::fromJson(file.read(1024 * 1024)).object();
}

bool writeEntry(const QString &toolsDir, const QString &section, const QString &key, const QJsonObject &entry)
{
    const QString path = QDir(toolsDir).filePath(QStringLiteral("tools.json"));
    QJsonObject state = readState(toolsDir);
    QJsonObject group = state.value(section).toObject();
    group.insert(key, entry);
    state.insert(section, group);
    if (!state.contains(QStringLiteral("installed"))) {
        state.insert(QStringLiteral("installed"), QJsonObject());
    }
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

// Version REAL de un binario: se le pregunta `--version` y se toma el primer numero con puntos
// ("2026.08.19", "deno 2.9.7 (stable...)"). Vacio si no arranca, si tarda demasiado o si no
// imprime nada reconocible. tools.json puede estar roto o mentir; el binario no.
QString probeVersion(const QString &binary)
{
    QProcess process;
    process.start(binary, {QStringLiteral("--version")});
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(2000);
        return QString();
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
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

// Pasa a la carpeta nueva lo que la vieja tenga descargado y verificado esperando el swap.
// Sin su entrada en tools.json no se puede registrar (falta version y sha), asi que se deja
// donde esta: perder un binario verificado es peor que repetir la descarga.
bool migrateStaged(const QString &legacyDir, const QString &targetDir, const QJsonObject &legacyState,
                   ToolsUpdater::Tool tool)
{
    const QString key = ToolsUpdater::toolKey(tool);
    const QString name = ToolsUpdater::binaryName(tool);
    const QString from = legacyDir + QStringLiteral("/.staging/") + name;
    if (!QFileInfo(from).isFile()) {
        return true;
    }
    const QJsonObject entry = legacyState.value(QStringLiteral("staged")).toObject().value(key).toObject();
    if (entry.value(QStringLiteral("version")).toString().isEmpty()) {
        qWarning() << "[AppPaths] Migracion: hay un" << key << "en staging sin entrada en tools.json, se deja";
        return false;
    }
    const QString stagingDir = targetDir + QStringLiteral("/.staging");
    if (!QDir().mkpath(stagingDir)) {
        return false;
    }
    const QString to = stagingDir + QLatin1Char('/') + name;
    QFile::remove(to);
    if (!moveFile(from, to)) {
        qWarning() << "[AppPaths] Migracion: no se pudo mover el staging de" << key;
        return false;
    }
    if (!writeEntry(targetDir, QStringLiteral("staged"), key, entry)) {
        qWarning() << "[AppPaths] Migracion: no se pudo registrar el staging de" << key;
        return false;
    }
    qInfo() << "[AppPaths] Migracion: staging de" << key << "movido a" << to;
    return true;
}

// Una tool instalada. Devuelve false si quedo algo atras (la carpeta vieja no se puede borrar).
bool migrateInstalled(const QString &legacyDir, const QString &targetDir, const QJsonObject &legacyState,
                      ToolsUpdater::Tool tool, QStringList *lines)
{
    const QString key = ToolsUpdater::toolKey(tool);
    const QString name = ToolsUpdater::binaryName(tool);
    const QString legacyBin = QDir(legacyDir).filePath(name);
    const QString targetBin = QDir(targetDir).filePath(name);
    if (!QFileInfo(legacyBin).isFile()) {
        return true;
    }

    // El binario de origen manda: si no responde --version no se mueve NI se borra.
    const QString legacyText = probeVersion(legacyBin);
    if (legacyText.isEmpty()) {
        qWarning() << "[AppPaths] Migracion:" << key << "de la carpeta anterior no responde --version, se deja";
        return false;
    }
    QVersionNumber legacyVersion = QVersionNumber::fromString(legacyText);
    const QJsonObject legacyEntry = legacyState.value(QStringLiteral("installed")).toObject().value(key).toObject();
    const QString jsonText = legacyEntry.value(QStringLiteral("version")).toString();
    // Se conserva sha/asset solo si tools.json coincide con lo que dijo el binario.
    const QJsonObject entry = (jsonText == legacyText)
                                  ? legacyEntry
                                  : QJsonObject{{QStringLiteral("version"), legacyText}};

    bool move = true;
    if (QFileInfo(targetBin).isFile()) {
        const QJsonObject targetInstalled = readState(targetDir).value(QStringLiteral("installed")).toObject();
        QString targetText = targetInstalled.value(key).toObject().value(QStringLiteral("version")).toString();
        if (targetText.isEmpty()) {
            targetText = probeVersion(targetBin); // copia del instalador, sin entrada en tools.json
        }
        const QVersionNumber targetVersion = QVersionNumber::fromString(targetText);
        if (targetVersion.isNull()) {
            // El de la app no arranca o no dice su version: gana el que si contesto.
            move = true;
        } else if (legacyVersion.isNull() || QVersionNumber::compare(legacyVersion, targetVersion) <= 0) {
            move = false;
            // Queda registrada la version probada del binario de la app: evita una descarga
            // al pedo en el primer arranque.
            if (targetInstalled.value(key).toObject().value(QStringLiteral("version")).toString().isEmpty()) {
                writeEntry(targetDir, QStringLiteral("installed"), key,
                           QJsonObject{{QStringLiteral("version"), targetText}});
            }
            qDebug() << "[AppPaths] Migracion:" << key << "de la app" << targetText
                     << "no es mas viejo que" << legacyText;
        }
    }

    if (!move) {
        return true; // duplicado con un binario mas nuevo y verificado en la app: se puede borrar
    }
    if (!QDir().mkpath(targetDir)) {
        return false;
    }

    // El binario actual se aparta a .old (cleanupLeftovers lo borra) y entra el nuevo.
    const QString oldPath = targetBin + QStringLiteral(".old.migrated");
    const bool hadTarget = QFileInfo::exists(targetBin);
    QFile::remove(oldPath);
    if (hadTarget && !QFile::rename(targetBin, oldPath)) {
        qWarning() << "[AppPaths] Migracion: no se pudo apartar" << targetBin;
        return false;
    }
    if (!moveFile(legacyBin, targetBin)) {
        if (hadTarget) {
            QFile::rename(oldPath, targetBin);
        }
        qWarning() << "[AppPaths] Migracion: no se pudo mover" << legacyBin << "(se reintenta)";
        return false;
    }
    QFile::remove(oldPath);
    if (!writeEntry(targetDir, QStringLiteral("installed"), key, entry)) {
        qWarning() << "[AppPaths] Migracion: no se pudo registrar" << key << "en tools.json";
    }
    if (lines) {
        lines->append(QStringLiteral("%1 %2 moved into the app folder").arg(key, legacyText));
    }
    qInfo() << "[AppPaths] Migracion:" << key << legacyText << "->" << targetBin;
    return true;
}

// Borra el contenido de la carpeta de cookies vieja y la carpeta. Lo que este en uso queda.
void removeLegacyCookies(const QString &dir)
{
    QDir cookies(dir);
    if (!cookies.exists()) {
        return;
    }
    const QStringList entries = cookies.entryList(QDir::Files | QDir::Hidden | QDir::System);
    for (const QString &entry : entries) {
        QFile::remove(cookies.filePath(entry));
    }
    QDir().rmdir(dir);
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

QStringList migrateToolsFolder(const QString &legacyDir, const QString &targetDir)
{
    QStringList lines;
#ifdef Q_OS_WIN
    if (!QDir(legacyDir).exists() || QDir::cleanPath(legacyDir) == QDir::cleanPath(targetDir)) {
        return lines;
    }
    const QJsonObject legacyState = readState(legacyDir);
    bool allMoved = true;
    for (ToolsUpdater::Tool tool : {ToolsUpdater::Tool::YtDlp, ToolsUpdater::Tool::Deno}) {
        if (!migrateStaged(legacyDir, targetDir, legacyState, tool)) {
            allMoved = false;
        }
        if (!migrateInstalled(legacyDir, targetDir, legacyState, tool, &lines)) {
            allMoved = false;
        }
    }

    // La carpeta vieja se borra SOLO si no quedo nada por migrar: si algo estaba en uso o no se
    // pudo verificar, se reintenta en el proximo arranque.
    if (!allMoved) {
        qWarning() << "[AppPaths] Migracion incompleta, se conserva" << legacyDir;
        return lines;
    }
    if (QDir(legacyDir).removeRecursively()) {
        qInfo() << "[AppPaths] Carpeta de tools anterior borrada:" << legacyDir;
    } else {
        qWarning() << "[AppPaths] No se pudo borrar entera" << legacyDir << "(se reintenta)";
    }
#else
    Q_UNUSED(legacyDir);
    Q_UNUSED(targetDir);
#endif
    return lines;
}

QStringList migrateLegacyWindowsData()
{
    QStringList lines;
#ifdef Q_OS_WIN
    // Con la carpeta de la app no escribible, LOCALAPPDATA ES la ubicacion vigente: no se toca.
    if (!usesFallback(QStringLiteral("tools"))) {
        lines = migrateToolsFolder(userDataDir(QStringLiteral("tools")), heavyDataDir(QStringLiteral("tools")));
    }
    if (!usesFallback(QStringLiteral("session-cookies"))) {
        // Los temporales de la ubicacion anterior son de descargas ya terminadas: se barren.
        removeLegacyCookies(userDataDir(QStringLiteral("session-cookies")));
    }
#endif
    return lines;
}

} // namespace AppPaths

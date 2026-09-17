#include "videodownloader/hostregistration.h"
#include "videodownloader/LgaRegistry.h"
#include "videodownloader/nativehost.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {

// Nombre de la carpeta de la extension, en el repo y junto a la app.
const QLatin1String kExtensionFolder("extension");

QString manifestFileName()
{
    return QString::fromLatin1(NativeHost::kHostName) + QStringLiteral(".json");
}

QJsonObject readJsonObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.read(64 * 1024));
    return doc.isObject() ? doc.object() : QJsonObject();
}

// Compara por contenido y no por bytes: el JSON del deploy y el que escribe la app pueden
// diferir en espacios.
bool manifestMatches(const QString &path, const QString &executablePath)
{
    const QJsonObject current = readJsonObject(path);
    const QJsonObject expected = QJsonDocument::fromJson(HostRegistration::hostManifest(executablePath)).object();
    return !current.isEmpty() && current == expected;
}

bool writeManifest(const QString &path, const QString &executablePath)
{
    if (manifestMatches(path, executablePath)) {
        return true;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(HostRegistration::hostManifest(executablePath));
    return file.commit();
}

#ifdef Q_OS_MACOS
QString bundleResourcesExtension()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../Resources/") + kExtensionFolder);
}

QString versionOf(const QString &folder)
{
    return readJsonObject(QDir(folder).filePath(QStringLiteral("manifest.json"))).value(QStringLiteral("version")).toString();
}

bool copyTree(const QString &from, const QString &to)
{
    QDir source(from);
    if (!QDir().mkpath(to)) {
        return false;
    }
    const QFileInfoList entries = source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString target = QDir(to).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyTree(entry.absoluteFilePath(), target)) {
                return false;
            }
        } else if (!QFile::copy(entry.absoluteFilePath(), target)) {
            return false;
        }
    }
    return true;
}
#endif

} // namespace

namespace HostRegistration {

QString extensionFolder()
{
#ifdef Q_OS_MACOS
    // El dialogo "Load unpacked" no entra en bundles: se carga la copia de Application Support.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(kExtensionFolder);
#else
    const QString besideApp = QDir(QCoreApplication::applicationDirPath()).filePath(kExtensionFolder);
#ifdef LGA_SOURCE_TREE_DIR
    // Build de desarrollo: la extension esta en el repo, no junto al exe.
    if (!QFileInfo(besideApp).isDir() && LgaRegistry::isDevelopmentBuild()) {
        const QString inRepo = QDir(QString::fromUtf8(LGA_SOURCE_TREE_DIR)).filePath(kExtensionFolder);
        if (QFileInfo(inRepo).isDir()) {
            return QDir::cleanPath(inRepo);
        }
    }
#endif
    return besideApp;
#endif
}

QString installedExtensionVersion()
{
    return readJsonObject(QDir(extensionFolder()).filePath(QStringLiteral("manifest.json")))
        .value(QStringLiteral("version")).toString();
}

QByteArray hostManifest(const QString &executablePath)
{
    const QJsonObject manifest{
        {QStringLiteral("name"), QString::fromLatin1(NativeHost::kHostName)},
        {QStringLiteral("description"), QStringLiteral("LGA Video Downloader")},
        {QStringLiteral("path"), executablePath},
        {QStringLiteral("type"), QStringLiteral("stdio")},
        {QStringLiteral("allowed_origins"), QJsonArray{NativeHost::allowedOrigin()}},
    };
    return QJsonDocument(manifest).toJson(QJsonDocument::Indented);
}

QString ensureRegistered()
{
    const bool devTree = LgaRegistry::isDevelopmentBuild();
#ifdef Q_OS_WIN
    const QString keyPath = QStringLiteral("HKEY_CURRENT_USER\\Software\\Google\\Chrome\\NativeMessagingHosts\\")
                            + QString::fromLatin1(NativeHost::kHostName);
    QSettings key(keyPath, QSettings::NativeFormat);
    const QString registered = key.value(QStringLiteral(".")).toString();
    if (devTree && !registered.isEmpty() && QFileInfo::exists(registered)) {
        // Desde el arbol de build no se pisa lo que dejo otra copia (la instalada).
        return QString();
    }

    // JSON junto al exe con path relativo (Chrome lo resuelve desde la carpeta del JSON). Si la
    // carpeta no es escribible, en AppLocalData con path absoluto.
    const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    QString manifestPath = QDir(QCoreApplication::applicationDirPath()).filePath(manifestFileName());
    if (!writeManifest(manifestPath, exeName)) {
        manifestPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                           .filePath(QStringLiteral("native-host/") + manifestFileName());
        if (!writeManifest(manifestPath, QDir::toNativeSeparators(QCoreApplication::applicationFilePath()))) {
            return QStringLiteral("Browser extension: couldn't write the connection file");
        }
    }
    const QString nativeManifest = QDir::toNativeSeparators(manifestPath);
    if (registered.compare(nativeManifest, Qt::CaseInsensitive) == 0) {
        return QString();
    }
    key.setValue(QStringLiteral("."), nativeManifest);
    key.sync();
    if (key.status() != QSettings::NoError) {
        return QStringLiteral("Browser extension: couldn't register the connection");
    }
    return QStringLiteral("Browser extension: connection registered");
#elif defined(Q_OS_MACOS)
    // No verificado en Mac. La extension se copia a Application Support (el dialogo de Load
    // unpacked no entra en bundles) y el JSON va con path absoluto en cada navegador instalado.
    QStringList changes;
    const QString source = bundleResourcesExtension();
    const QString target = extensionFolder();
    const bool targetExists = QFileInfo(target).isDir();
    if (QFileInfo(source).isDir() && (!targetExists || (!devTree && versionOf(source) != versionOf(target)))) {
        QDir(target).removeRecursively();
        if (copyTree(source, target)) {
            changes << QStringLiteral("extension copied");
        }
    }
    const QString support = QDir::homePath() + QStringLiteral("/Library/Application Support/");
    const QStringList browsers = {QStringLiteral("Google/Chrome"), QStringLiteral("BraveSoftware/Brave-Browser"),
                                  QStringLiteral("Microsoft Edge")};
    const QString executable = QCoreApplication::applicationFilePath();
    for (const QString &browser : browsers) {
        if (!QFileInfo(support + browser).isDir()) {
            continue;
        }
        const QString path = support + browser + QStringLiteral("/NativeMessagingHosts/") + manifestFileName();
        if (devTree && QFileInfo::exists(path)) {
            continue;
        }
        if (!manifestMatches(path, executable) && writeManifest(path, executable)) {
            changes << browser;
        }
    }
    return changes.isEmpty() ? QString() : QStringLiteral("Browser extension: %1").arg(changes.join(QStringLiteral(", ")));
#else
    Q_UNUSED(devTree);
    return QString();
#endif
}

} // namespace HostRegistration

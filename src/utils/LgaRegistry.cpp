#include "videodownloader/LgaRegistry.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtGlobal>

namespace {

/**
 * En macOS `AppDataLocation` devuelve `~/Library/Application Support/<Org>/<App>` y en Windows
 * `%APPDATA%/<Org>/<App>`. Nosotros queremos el nivel de la ORGANIZACION —`.../LGA`—, que es
 * comun a todas las apps, asi que se sube un nivel desde el de la app.
 */
QString resolveRegistryDir()
{
    const QString appDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDir.isEmpty()) {
        return QString();
    }
    QDir dir(appDir);
    // AppDataLocation ya incluye el nombre de la app; el padre es el de la organizacion.
    // El guard NO es decorativo: Qt saltea los componentes vacios al armar la ruta, asi que una
    // app que setea organizationName pero NO applicationName ya tiene `.../LGA` aca, y el cdUp
    // aterrizaria un nivel mas arriba —fuera del registro— sin error y sin log.
    if (dir.dirName() != QCoreApplication::applicationName() || !dir.cdUp()) {
        return QString();
    }
    return dir.absolutePath();
}

/** Escritura atomica: nadie puede leer un JSON a medio escribir. */
bool writeJson(const QString& filePath, const QJsonObject& obj)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("LgaRegistry: no se pudo abrir para escribir %s", qUtf8Printable(filePath));
        return false;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning("LgaRegistry: no se pudo commitear %s", qUtf8Printable(filePath));
        return false;
    }
    return true;
}

QJsonObject readJson(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QJsonObject();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

QString nowIso()
{
    // UTC y no hora local: `currentDateTime()` con ISODate no emite offset, asi que dos
    // maquinas en husos distintos producen timestamps que no se pueden comparar.
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

/**
 * La carpeta que CONTIENE la app, que es lo que sirve para ubicarla o relanzarla:
 * en macOS el `.app` completo (el ejecutable vive tres niveles adentro, en
 * `Contents/MacOS/`), en Windows el directorio del `.exe`.
 */
QString resolveInstallPath()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    QDir dir(exeDir);
    if (dir.dirName() == QLatin1String("MacOS") && dir.cdUp() && dir.cdUp()) {
        if (dir.dirName().endsWith(QLatin1String(".app"))) {
            return QDir::toNativeSeparators(dir.absolutePath());
        }
    }
#endif
    return QDir::toNativeSeparators(exeDir);
}

/**
 * Nombre de la carpeta que CONTIENE la app: la que tiene al `.app` en macOS, la del `.exe` en
 * Windows. Solo esa, no las de mas arriba.
 */
QString containerFolderName()
{
    const QString installPath = QDir::fromNativeSeparators(resolveInstallPath());
#ifdef Q_OS_MACOS
    // `resolveInstallPath()` devuelve el `.app`; la carpeta que lo contiene es su padre.
    if (installPath.endsWith(QLatin1String(".app"))) {
        return QFileInfo(installPath).absoluteDir().dirName();
    }
#endif
    // En Windows `installPath` YA es la carpeta del `.exe`.
    return QDir(installPath).dirName();
}

/** Si `appPath` cuelga de `rootDir`. Vacio o no contenido => false. */
bool pathIsInside(const QString& appPath, const char* rootDirLiteral)
{
    const QString rootDir = QDir::cleanPath(QDir::fromNativeSeparators(QLatin1String(rootDirLiteral)));
    if (rootDir.isEmpty()) {
        return false;
    }
#ifdef Q_OS_WIN
    // En Windows la misma ruta puede llegar con distinta capitalizacion (`C:` vs `c:`).
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    // Se compara por COMPONENTE de ruta: un `startsWith` a secas haria que `/x/build2` cuente
    // como si estuviera adentro de `/x/build`.
    return appPath.startsWith(rootDir + QLatin1Char('/'), cs);
}

/**
 * Si este binario esta corriendo desde una salida de DESARROLLO y no desde una instalacion.
 *
 * Es el unico caso que NO se registra. Todo lo demas si: `/Applications`, `Program Files`, una
 * carpeta cualquiera del usuario. La regla es una lista NEGRA, y no una lista blanca de
 * ubicaciones validas, porque la app se puede instalar en cualquier lado y una lista blanca
 * dejaria sin registrar instalaciones legitimas.
 *
 * Que problema resuelve: el registro se escribia en CADA arranque, asi que el ultimo binario
 * abierto ganaba. En desarrollo ese es siempre el del build, y el card de LGA Updates de
 * PipeSync terminaba mostrando la ruta del arbol de compilacion en vez de la instalada.
 *
 * Hay DOS chequeos, y se complementan:
 *
 * 1. **El nombre de la carpeta que contiene la app** — `build`, `build-release`, `deploy`, ...
 *    Se mira SOLO esa carpeta y no la ruta entera: `D:\Builds\Apps\X.exe` es una instalacion
 *    legitima y se registra, porque la carpeta de la app es `Apps`. Mirar la ruta completa
 *    volteria esa instalacion en silencio, que es este mismo bug al reves.
 *    Ventaja: NO depende de que nadie cablee nada, asi que sigue valiendo en una app que se
 *    olvido del punto 2.
 *
 * 2. **Las rutas reales del proyecto**, que inyecta CMake:
 *      - `LGA_BUILD_TREE_DIR` (`CMAKE_BINARY_DIR`) — el arbol de build, se llame como se llame.
 *      - `LGA_SOURCE_TREE_DIR` (`CMAKE_SOURCE_DIR`) — cubre `deploy/` y cualquier otro staging
 *        dentro del repo, que son hermanos del build y no hijos.
 *    Ventaja: agarra lo que el nombre no ve, como un arbol de build llamado `out/`.
 */
bool runsFromDevTree()
{
    const QString folder = containerFolderName();
    // `contains` y no `startsWith`: asi entra `build-release` y cualquier variante. La contra es
    // que una instalacion en una carpeta llamada `Builds` tampoco registra; se acepta porque el
    // nombre de la carpeta que contiene a la app rara vez es eso en una instalacion de verdad.
    if (folder.contains(QLatin1String("build"), Qt::CaseInsensitive)
        || folder.contains(QLatin1String("deploy"), Qt::CaseInsensitive)) {
        return true;
    }

#if defined(LGA_BUILD_TREE_DIR) || defined(LGA_SOURCE_TREE_DIR)
    const QString appPath =
        QDir::cleanPath(QDir::fromNativeSeparators(QCoreApplication::applicationFilePath()));
#ifdef LGA_BUILD_TREE_DIR
    if (pathIsInside(appPath, LGA_BUILD_TREE_DIR)) {
        return true;
    }
#endif
#ifdef LGA_SOURCE_TREE_DIR
    if (pathIsInside(appPath, LGA_SOURCE_TREE_DIR)) {
        return true;
    }
#endif
#endif
    return false;
}

} // namespace

namespace LgaRegistry {

bool isDevelopmentBuild()
{
    return runsFromDevTree();
}

QString directory()
{
    const QString dirPath = resolveRegistryDir();
    if (dirPath.isEmpty()) {
        return QString();
    }
    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        qWarning("LgaRegistry: no se pudo crear %s", qUtf8Printable(dirPath));
        return QString();
    }
    return dirPath;
}

bool registerThisApp(const QString& appName, const QString& version)
{
    const QString dirPath = directory();
    if (dirPath.isEmpty() || appName.trimmed().isEmpty()) {
        return false;
    }

    // El arbol de desarrollo no se registra: pisaria a la copia instalada, que es la que le
    // sirve a las otras apps. Va al log —y no en silencio— porque si no la proxima vez que el
    // registro no tenga lo que se espera hay que salir a buscar a ciegas por que.
    if (runsFromDevTree()) {
        qInfo("LgaRegistry: %s corre desde el arbol de desarrollo (%s), no se registra para no "
              "pisar la copia instalada",
              qUtf8Printable(appName),
              qUtf8Printable(resolveInstallPath()));
        return false;
    }

    QJsonObject obj;
    obj[QStringLiteral("name")] = appName;
    obj[QStringLiteral("version")] = version;
    obj[QStringLiteral("installPath")] = resolveInstallPath();
    obj[QStringLiteral("executable")] =
        QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    obj[QStringLiteral("updatedAt")] = nowIso();

    const QString filePath = QDir(dirPath).filePath(appName + QStringLiteral(".json"));
    const bool ok = writeJson(filePath, obj);
    if (ok) {
        qInfo("LgaRegistry: registrada %s v%s en %s",
              qUtf8Printable(appName),
              qUtf8Printable(version),
              qUtf8Printable(obj[QStringLiteral("installPath")].toString()));
    }
    return ok;
}

bool saveNukeDirectory(const QString& nukeDir)
{
    const QString dirPath = directory();
    if (dirPath.isEmpty()) {
        return false;
    }
    const QString clean = QDir::cleanPath(nukeDir.trimmed());
    if (clean.isEmpty()) {
        return false;
    }

    const QString filePath = QDir(dirPath).filePath(QStringLiteral("nuke.json"));
    QJsonObject obj = readJson(filePath);
    obj[QStringLiteral("nukeDir")] = QDir::toNativeSeparators(clean);
    obj[QStringLiteral("updatedAt")] = nowIso();

    const bool ok = writeJson(filePath, obj);
    if (ok) {
        qInfo("LgaRegistry: carpeta .nuke registrada en %s", qUtf8Printable(clean));
    }
    return ok;
}

QString readNukeDirectory()
{
    const QString dirPath = resolveRegistryDir();
    if (dirPath.isEmpty()) {
        return QString();
    }
    const QJsonObject obj = readJson(QDir(dirPath).filePath(QStringLiteral("nuke.json")));
    return obj.value(QStringLiteral("nukeDir")).toString();
}

} // namespace LgaRegistry

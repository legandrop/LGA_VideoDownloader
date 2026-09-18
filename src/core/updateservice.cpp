#include "videodownloader/updateservice.h"
#include "videodownloader/LgaRegistry.h"
#include "videodownloader/apppaths.h"
#include "videodownloader/toolsupdater.h"
#include "videodownloader/updateurls.h"
#include "videodownloader/versioncompare.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

namespace {

const QString kRepoSlug = QStringLiteral("legandrop/LGA_VideoDownloader");
const QString kSumsName = QStringLiteral("SHA256SUMS");
constexpr int kCheckTimeoutMs = 15000;
constexpr int kDownloadIdleTimeoutMs = 120000;
constexpr qint64 kMaxSumsBytes = 1024 * 1024;

QString userAgent()
{
    return QStringLiteral("LGA_VideoDownloader/%1").arg(QCoreApplication::applicationVersion());
}

constexpr int kResweepDelayMs = 3 * 60 * 1000;

QString updateDirPath()
{
#ifdef Q_OS_WIN
    // Dentro de la instalacion (`<app>/updates`), con fallback a LOCALAPPDATA si la carpeta de
    // la app no acepta una escritura real: la regla LGA de datos pesados (AppPaths).
    return AppPaths::heavyDataDir(QStringLiteral("updates"));
#else
    // macOS no baja el instalador (abre la pagina del release); si algun dia lo hace, fuera
    // del bundle.
    return UpdateService::legacyTempUpdateDir();
#endif
}

} // namespace

// Carpeta de updates de las versiones <= 0.95 en Windows (y la vigente en macOS). Sale de
// TempLocation, que en Windows respeta TMP/TEMP.
QString UpdateService::legacyTempUpdateDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("LGA_VideoDownloader_updates"));
}

// Todas las carpetas donde puede haber quedado un instalador: la de la instalacion, la del
// fallback (una instalacion que alguna vez no fue escribible) y la de %TEMP% de antes.
QStringList UpdateService::installerSweepDirs(const QString &appDir, const QString &fallbackDir)
{
    QStringList dirs;
#ifdef Q_OS_WIN
    dirs << QDir(appDir).filePath(QStringLiteral("updates")) << fallbackDir;
#else
    Q_UNUSED(appDir);
    Q_UNUSED(fallbackDir);
#endif
    const QString legacy = legacyTempUpdateDir();
    if (!dirs.contains(legacy)) {
        dirs << legacy;
    }
    return dirs;
}

// Borra los instaladores de updates (~80 MB cada uno) que hayan quedado en `dirs`, salvo
// keepName. Solo archivos con nombre de asset: lo demas que haya en esas carpetas no se toca.
// Si uno sigue en uso (el instalador todavia corriendo) el borrado falla y se reintenta la
// proxima vez. Una carpeta que queda vacia se borra. Devuelve cuantos borro.
int UpdateService::removeOldInstallers(const QStringList &dirs, const QString &keepName)
{
    int removed = 0;
    const QStringList filters = {QStringLiteral("VideoDownloader_Setup_v*.exe"),
                                 QStringLiteral("LGA_Video_Downloader_Mac_v*.zip")};
    for (const QString &path : dirs) {
        QDir dir(path);
        if (path.isEmpty() || !dir.exists()) {
            continue;
        }
        const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files | QDir::Hidden | QDir::NoSymLinks);
        for (const QFileInfo &entry : entries) {
            if (entry.fileName() == keepName) {
                continue;
            }
            if (QFile::remove(entry.absoluteFilePath())) {
                ++removed;
                qDebug() << "[UpdateService] Instalador viejo borrado" << entry.absoluteFilePath();
            } else {
                qDebug() << "[UpdateService] No se pudo borrar el instalador viejo" << entry.absoluteFilePath();
            }
        }
        // rmdir solo borra una carpeta vacia: si queda cualquier otra cosa, no pasa nada.
        QDir().rmdir(path);
    }
    return removed;
}

void UpdateService::sweepInstallers(const QString &keepName)
{
    removeOldInstallers(installerSweepDirs(QCoreApplication::applicationDirPath(),
                                           AppPaths::userDataDir(QStringLiteral("updates"))),
                        keepName);
}

// Asset de update por plataforma. En Windows el instalador conserva el nombre
// VideoDownloader (regla del repo); en macOS es el .zip que produce deploy.sh.
QString UpdateService::assetNameFor(const QString &version)
{
#ifdef Q_OS_WIN
    return QStringLiteral("VideoDownloader_Setup_v%1.exe").arg(version);
#else
    return QStringLiteral("LGA_Video_Downloader_Mac_v%1.zip").arg(version);
#endif
}

UpdateService::UpdateService(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    sweepInstallers(QString());
    // Despues de un update, la app nueva arranca mientras el instalador todavia corre (el
    // [Run] de Inno la lanza antes de terminar) y el barrido de arriba no puede borrarlo. Se
    // reintenta una vez mas tarde, en la misma sesion.
    QTimer::singleShot(kResweepDelayMs, this, [this]() {
        if (m_state != State::Downloading && m_state != State::Installing) {
            sweepInstallers(m_assetName);
        }
    });
}

UpdateService::~UpdateService()
{
    for (QNetworkReply *reply : {m_checkReply, m_downloadReply}) {
        if (reply) {
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    }
    m_checkReply = nullptr;
    m_downloadReply = nullptr;
    discardPartialDownload();
}

QString UpdateService::currentVersion() const
{
    return QCoreApplication::applicationVersion();
}

bool UpdateService::installsInPlace()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

QString UpdateService::installBlockedReason() const
{
    if (installsInPlace() && LgaRegistry::isDevelopmentBuild()) {
        return QStringLiteral("Updates are disabled in development builds.");
    }
    return QString();
}

void UpdateService::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(state);
}

void UpdateService::checkForUpdates()
{
    if (m_checkReply || m_state == State::Downloading || m_state == State::Installing) {
        return;
    }
    const QUrl url(QStringLiteral("%1/%2/releases/latest").arg(UpdateUrls::githubBase(), kRepoSlug));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    // Sin seguir el redirect: el Location trae el tag del ultimo release.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(kCheckTimeoutMs);

    m_errorString.clear();
    m_checkTag.clear();
    m_checkVersion.clear();
    setState(State::Checking);
    qDebug() << "[UpdateService] Chequeando" << url.toString();
    m_checkReply = m_network->get(request);
    connect(m_checkReply, &QNetworkReply::finished, this, &UpdateService::onLatestFinished);
}

void UpdateService::failCheck(const QString &message)
{
    qDebug() << "[UpdateService] Chequeo fallido:" << message;
    m_errorString = message;
    m_availableVersion.clear();
    setState(State::CheckFailed);
}

void UpdateService::onLatestFinished()
{
    QNetworkReply *reply = m_checkReply;
    m_checkReply = nullptr;
    if (!reply) {
        return;
    }
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QUrl target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (target.isEmpty() && reply->hasRawHeader("Location")) {
        target = QUrl(QString::fromUtf8(reply->rawHeader("Location")));
    }
    if (reply->error() != QNetworkReply::NoError || (status != 301 && status != 302) || target.isEmpty()) {
        failCheck(reply->error() != QNetworkReply::NoError
                      ? QStringLiteral("Could not check for updates (%1).").arg(reply->errorString())
                      : QStringLiteral("Could not check for updates (HTTP %1).").arg(status));
        return;
    }

    const QString path = reply->url().resolved(target).path();
    const QString marker = QStringLiteral("/releases/tag/");
    const int idx = path.indexOf(marker);
    if (idx < 0) {
        // Un repo sin releases redirige a /releases: no hay update, no es un error.
        qDebug() << "[UpdateService] Sin releases publicados";
        m_availableVersion.clear();
        setState(State::UpToDate);
        return;
    }
    const QString tag = path.mid(idx + marker.size());
    static const QRegularExpression tagRe(QStringLiteral("^v?([0-9]+(?:\\.[0-9]+)+)$"));
    const QRegularExpressionMatch match = tagRe.match(tag);
    if (!match.hasMatch()) {
        failCheck(QStringLiteral("Could not check for updates (unexpected release tag '%1').").arg(tag));
        return;
    }
    const QString version = match.captured(1);
    if (!VersionCompare::isNewer(version, currentVersion())) {
        qDebug() << "[UpdateService] Al dia. Remoto:" << version << "local:" << currentVersion();
        m_availableVersion.clear();
        setState(State::UpToDate);
        return;
    }

    // Hay version nueva: sin hash verificable no se ofrece (fail-closed).
    m_checkTag = tag;
    m_checkVersion = version;
    QNetworkRequest request(QUrl(QStringLiteral("%1/%2/releases/download/%3/%4")
                                     .arg(UpdateUrls::githubBase(), kRepoSlug, tag, kSumsName)));
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setTransferTimeout(kCheckTimeoutMs);
    m_checkReply = m_network->get(request);
    connect(m_checkReply, &QNetworkReply::finished, this, &UpdateService::onSumsFinished);
}

void UpdateService::onSumsFinished()
{
    QNetworkReply *reply = m_checkReply;
    m_checkReply = nullptr;
    if (!reply) {
        return;
    }
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status != 200 || reply->bytesAvailable() > kMaxSumsBytes) {
        failCheck(QStringLiteral("Version %1 is available but cannot be verified (no %2).").arg(m_checkVersion, kSumsName));
        return;
    }
    const QString assetName = assetNameFor(m_checkVersion);
    const QString digest = ToolsUpdater::parseSha256(reply->readAll(), assetName);
    if (digest.isEmpty()) {
        failCheck(QStringLiteral("Version %1 is available but cannot be verified (%2 has no hash for %3).")
                      .arg(m_checkVersion, kSumsName, assetName));
        return;
    }

    const QString base = UpdateUrls::githubBase();
    m_availableVersion = m_checkVersion;
    m_assetName = assetName;
    m_assetDigest = digest;
    m_assetUrl = QUrl(QStringLiteral("%1/%2/releases/download/%3/%4").arg(base, kRepoSlug, m_checkTag, assetName));
    m_releasePageUrl = QUrl(QStringLiteral("%1/%2/releases/tag/%3").arg(base, kRepoSlug, m_checkTag));
    qInfo() << "[UpdateService] Update disponible:" << m_availableVersion << m_assetUrl.toString();
    setState(State::UpdateAvailable);
    emit appUpdateAvailable(m_availableVersion, m_releasePageUrl);
}

void UpdateService::installAppUpdate()
{
    if (m_availableVersion.isEmpty() || m_downloadReply
        || (m_state != State::UpdateAvailable && m_state != State::InstallFailed)) {
        return;
    }

    if (!installsInPlace()) {
        // macOS: el reemplazo del bundle queda para mas adelante; se abre el release.
        QDesktopServices::openUrl(m_releasePageUrl);
        return;
    }

    const QString blocked = installBlockedReason();
    if (!blocked.isEmpty()) {
        failInstall(blocked);
        return;
    }

    // El barrido va ANTES del mkpath: borra las carpetas de updates que quedan vacias.
    discardPartialDownload();
    sweepInstallers(m_assetName);
    const QString updateDir = updateDirPath();
    if (!QDir().mkpath(updateDir)) {
        failInstall(QStringLiteral("The update folder could not be created."));
        return;
    }
    m_downloadTargetPath = QDir(updateDir).filePath(m_assetName);
    m_downloadFile = new QSaveFile(m_downloadTargetPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        failInstall(QStringLiteral("The update installer could not be saved (%1).").arg(m_downloadFile->errorString()));
        return;
    }
    m_downloadHash = new QCryptographicHash(QCryptographicHash::Sha256);

    QNetworkRequest request(m_assetUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setTransferTimeout(kDownloadIdleTimeoutMs);

    m_errorString.clear();
    setState(State::Downloading);
    qDebug() << "[UpdateService] Descargando instalador" << m_assetUrl.toString();
    m_downloadReply = m_network->get(request);
    connect(m_downloadReply, &QNetworkReply::readyRead, this, &UpdateService::onDownloadReadyRead);
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, &UpdateService::installProgress);
    connect(m_downloadReply, &QNetworkReply::finished, this, &UpdateService::onDownloadFinished);
}

void UpdateService::cancelInstall()
{
    if (m_downloadReply) {
        m_downloadCancelled = true;
        m_downloadReply->abort();
    }
}

void UpdateService::onDownloadReadyRead()
{
    if (!m_downloadReply || !m_downloadFile || m_downloadWriteFailed) {
        return;
    }
    if (!m_downloadResponseChecked) {
        m_downloadResponseChecked = true;
        const int status = m_downloadReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            m_downloadWriteFailed = true;
            QTimer::singleShot(0, this, [this]() {
                if (m_downloadReply) {
                    m_downloadReply->abort();
                }
            });
            return;
        }
    }
    const QByteArray chunk = m_downloadReply->readAll();
    if (chunk.isEmpty()) {
        return;
    }
    if (m_downloadFile->write(chunk) != chunk.size()) {
        m_downloadWriteFailed = true;
        QTimer::singleShot(0, this, [this]() {
            if (m_downloadReply) {
                m_downloadReply->abort();
            }
        });
        return;
    }
    m_downloadHash->addData(chunk);
}

void UpdateService::onDownloadFinished()
{
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    if (!reply) {
        return;
    }
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!m_downloadWriteFailed && m_downloadFile && reply->bytesAvailable() > 0 && status == 200) {
        const QByteArray tail = reply->readAll();
        if (m_downloadFile->write(tail) != tail.size()) {
            m_downloadWriteFailed = true;
        } else {
            m_downloadHash->addData(tail);
        }
    }

    if (m_downloadCancelled) {
        qDebug() << "[UpdateService] Descarga cancelada por el usuario";
        discardPartialDownload();
        setState(State::UpdateAvailable);
        return;
    }
    if (reply->error() != QNetworkReply::NoError || status != 200 || m_downloadWriteFailed) {
        failInstall(status != 200 && status != 0
                        ? QStringLiteral("The update could not be downloaded (HTTP %1).").arg(status)
                        : (m_downloadWriteFailed ? QStringLiteral("The update installer could not be written to disk.")
                                                 : QStringLiteral("The update could not be downloaded (%1).").arg(reply->errorString())));
        return;
    }

    const QString got = QString::fromLatin1(m_downloadHash->result().toHex());
    if (got.compare(m_assetDigest, Qt::CaseInsensitive) != 0) {
        qWarning() << "[UpdateService] Hash distinto. Esperado" << m_assetDigest << "obtenido" << got;
        failInstall(QStringLiteral("The downloaded update failed integrity verification."));
        return;
    }
    if (!m_downloadFile->commit()) {
        failInstall(QStringLiteral("The update installer could not be saved (%1).").arg(m_downloadFile->errorString()));
        return;
    }
    const QString installerPath = m_downloadTargetPath;
    delete m_downloadFile;
    m_downloadFile = nullptr;
    delete m_downloadHash;
    m_downloadHash = nullptr;
    launchInstaller(installerPath);
}

void UpdateService::launchInstaller(const QString &installerPath)
{
    // Primero se corta la cola y se matan yt-dlp y sus hijos (ffmpeg, deno): el instalador
    // solo mata VideoDownloader.exe y un binario de {app}\tools en uso bloquearia la copia.
    if (m_beforeInstallHook) {
        m_beforeInstallHook();
    }

    const QString appDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    QProcess process;
    process.setProgram(installerPath);
#ifdef Q_OS_WIN
    // /DIR: instalar sobre ESTA copia (el .iss tiene UsePreviousAppDir=no y sin esto una
    // instalacion fuera de la ruta default se duplicaria). !desktopicon: un update no
    // recrea el acceso directo del escritorio si el usuario lo borro.
    process.setNativeArguments(QStringLiteral("/SILENT /SUPPRESSMSGBOXES /NORESTART /MERGETASKS=\"!desktopicon\" /DIR=\"%1\"")
                                   .arg(appDir));
#endif
    qInfo() << "[UpdateService] Lanzando instalador" << installerPath << "sobre" << appDir;
    if (!process.startDetached()) {
        // Sin lanzarse no sirve: no se deja el instalador ocupando lugar.
        QFile::remove(installerPath);
        failInstall(QStringLiteral("The update installer could not be started."));
        return;
    }
    setState(State::Installing);
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void UpdateService::failInstall(const QString &message)
{
    qWarning() << "[UpdateService] Instalacion fallida:" << message;
    discardPartialDownload();
    m_errorString = message;
    setState(State::InstallFailed);
}

void UpdateService::discardPartialDownload()
{
    if (m_downloadFile) {
        m_downloadFile->cancelWriting();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }
    delete m_downloadHash;
    m_downloadHash = nullptr;
    m_downloadTargetPath.clear();
    m_downloadCancelled = false;
    m_downloadWriteFailed = false;
    m_downloadResponseChecked = false;
}

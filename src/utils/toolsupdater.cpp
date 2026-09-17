#include "videodownloader/toolsupdater.h"
#include "videodownloader/updateurls.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QVersionNumber>

namespace {

// deno se actualiza solo dentro de este major: un major nuevo es el unico salto con riesgo
// real de romper la integracion con yt-dlp, y se revisa a mano.
constexpr int kDenoMajor = 2;

constexpr int kMetaTimeoutMs = 30000;     // redirect del tag y archivo de sumas
constexpr int kAssetIdleTimeoutMs = 60000; // inactividad de la descarga, no duracion total
constexpr int kExtractTimeoutMs = 120000;
constexpr int kSmokeTimeoutMs = 60000;     // yt-dlp onefile tarda en arrancar la primera vez
constexpr qint64 kMaxSumsBytes = 1024 * 1024;

QString stagingDir() { return ToolsUpdater::toolsDir() + QStringLiteral("/.staging"); }
QString downloadDir() { return stagingDir() + QStringLiteral("/dl"); }
QString extractDir() { return stagingDir() + QStringLiteral("/extract"); }
QString stateFilePath() { return ToolsUpdater::toolsDir() + QStringLiteral("/tools.json"); }

QJsonObject readState()
{
    QFile file(stateFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.read(kMaxSumsBytes));
    return doc.isObject() ? doc.object() : QJsonObject();
}

bool writeState(const QJsonObject &state)
{
    if (!QDir().mkpath(ToolsUpdater::toolsDir())) {
        return false;
    }
    QSaveFile file(stateFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray bytes = QJsonDocument(state).toJson(QJsonDocument::Indented);
    return file.write(bytes) == bytes.size() && file.commit();
}

void makeExecutable(const QString &path)
{
#ifdef Q_OS_MAC
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                    | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                    | QFileDevice::ReadOther | QFileDevice::ExeOther);
#else
    Q_UNUSED(path);
#endif
}

// Rename con reintentos cortos: en Windows el antivirus puede tener abierto un .exe recien
// escrito durante unos cientos de milisegundos.
bool renameWithRetry(const QString &from, const QString &to)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (QFile::rename(from, to)) {
            return true;
        }
        QThread::msleep(150);
    }
    return false;
}

bool parseVersion(const QString &text, QVersionNumber *out)
{
    qsizetype suffix = -1;
    const QVersionNumber v = QVersionNumber::fromString(text, &suffix);
    if (v.isNull() || suffix != text.size()) {
        return false;
    }
    *out = v;
    return true;
}

} // namespace

ToolsUpdater::ToolsUpdater(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_githubBase(UpdateUrls::githubBase())
{
}

ToolsUpdater::~ToolsUpdater()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
    }
    discardDownload();
}

QString ToolsUpdater::toolsDir()
{
    // GenericDataLocation: %LOCALAPPDATA% en Windows (no Roaming: 20-90 MB no deben viajar
    // con el perfil) y ~/Library/Application Support en macOS.
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/LGA/VideoDownloader/tools");
}

QString ToolsUpdater::binaryName(Tool tool)
{
#ifdef Q_OS_WIN
    return tool == Tool::YtDlp ? QStringLiteral("yt-dlp.exe") : QStringLiteral("deno.exe");
#else
    return tool == Tool::YtDlp ? QStringLiteral("yt-dlp") : QStringLiteral("deno");
#endif
}

QString ToolsUpdater::toolKey(Tool tool)
{
    return tool == Tool::YtDlp ? QStringLiteral("yt-dlp") : QStringLiteral("deno");
}

QString ToolsUpdater::installedBinary(Tool tool)
{
    const QString path = toolsDir() + QLatin1Char('/') + binaryName(tool);
    return QFileInfo(path).isFile() ? path : QString();
}

QString ToolsUpdater::installedVersion(Tool tool)
{
    if (installedBinary(tool).isEmpty()) {
        return QString();
    }
    return readState().value(QStringLiteral("installed")).toObject()
        .value(toolKey(tool)).toObject().value(QStringLiteral("version")).toString();
}

QString ToolsUpdater::parseSha256(const QByteArray &sumsBody, const QString &assetName)
{
    // Hash de exactamente 64 hex: los lookarounds evitan tomar un pedazo de un hex mas largo.
    static const QRegularExpression hashRe(
        QStringLiteral("(?<![0-9a-fA-F])([0-9a-fA-F]{64})(?![0-9a-fA-F])"));

    const QString text = QString::fromUtf8(sumsBody);
    if (assetName.isEmpty()) {
        const QRegularExpressionMatch match = hashRe.match(text);
        return match.hasMatch() ? match.captured(1).toLower() : QString();
    }

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QRegularExpressionMatch match = hashRe.match(rawLine);
        if (!match.hasMatch()) {
            continue;
        }
        QString name = rawLine.mid(match.capturedEnd(1)).trimmed();
        if (name.startsWith(QLatin1Char('*'))) {
            name.remove(0, 1); // modo binario de sha256sum
        }
        if (name == assetName) {
            return match.captured(1).toLower();
        }
    }
    return QString();
}

QStringList ToolsUpdater::applyStaged(QStringList *logLines)
{
    QStringList swapped;
    QJsonObject state = readState();
    QJsonObject staged = state.value(QStringLiteral("staged")).toObject();
    if (staged.isEmpty()) {
        return swapped;
    }
    QJsonObject installed = state.value(QStringLiteral("installed")).toObject();

    for (Tool tool : {Tool::YtDlp, Tool::Deno}) {
        const QString key = toolKey(tool);
        if (!staged.contains(key)) {
            continue;
        }
        const QJsonObject entry = staged.value(key).toObject();
        const QString stagedPath = stagingDir() + QLatin1Char('/') + binaryName(tool);
        const QString finalPath = toolsDir() + QLatin1Char('/') + binaryName(tool);

        if (!QFileInfo(stagedPath).isFile()) {
            qWarning() << "[ToolsUpdater] Entrada staged sin archivo, se descarta:" << key;
            staged.remove(key);
            continue;
        }

        // final -> .old (un .exe en uso se puede renombrar pero no borrar en Windows).
        QString oldPath = finalPath + QStringLiteral(".old");
        if (QFileInfo::exists(oldPath) && !QFile::remove(oldPath)) {
            oldPath = finalPath + QStringLiteral(".old.%1").arg(QDateTime::currentMSecsSinceEpoch());
        }
        const bool hadFinal = QFileInfo::exists(finalPath);
        if (hadFinal && !renameWithRetry(finalPath, oldPath)) {
            qWarning() << "[ToolsUpdater] No se pudo apartar el binario actual:" << finalPath;
            if (logLines) {
                logLines->append(QStringLiteral("%1: could not replace the current binary, will retry later").arg(key));
            }
            continue;
        }
        if (!renameWithRetry(stagedPath, finalPath)) {
            // Rollback: el binario anterior vuelve a su lugar y el staging queda para otro intento.
            if (hadFinal && !QFile::rename(oldPath, finalPath)) {
                qCritical() << "[ToolsUpdater] Rollback fallido, queda en" << oldPath;
            }
            qWarning() << "[ToolsUpdater] No se pudo mover el staging al destino:" << stagedPath;
            if (logLines) {
                logLines->append(QStringLiteral("%1: could not activate the new binary, will retry later").arg(key));
            }
            continue;
        }
        makeExecutable(finalPath);
        QFile::remove(oldPath); // si esta en uso falla y lo borra el proximo arranque

        installed.insert(key, entry);
        staged.remove(key);
        swapped.append(key);
        if (logLines) {
            logLines->append(QStringLiteral("%1 %2 is now active").arg(key, entry.value(QStringLiteral("version")).toString()));
        }
        qInfo() << "[ToolsUpdater] Swap hecho:" << key << entry.value(QStringLiteral("version")).toString();
    }

    state.insert(QStringLiteral("installed"), installed);
    state.insert(QStringLiteral("staged"), staged);
    if (!writeState(state)) {
        qWarning() << "[ToolsUpdater] No se pudo escribir tools.json despues del swap";
    }
    return swapped;
}

void ToolsUpdater::cleanupLeftovers()
{
    QDir dir(toolsDir());
    if (!dir.exists()) {
        return;
    }
    const QStringList olds = dir.entryList({QStringLiteral("*.old"), QStringLiteral("*.old.*")}, QDir::Files);
    for (const QString &name : olds) {
        if (!dir.remove(name)) {
            qDebug() << "[ToolsUpdater] .old todavia en uso, queda para el proximo arranque:" << name;
        }
    }
    // Descargas y extracciones a medio terminar de una sesion anterior. Lo verificado
    // (el binario suelto en .staging) se conserva para el swap.
    QDir(downloadDir()).removeRecursively();
    QDir(extractDir()).removeRecursively();
}

QString ToolsUpdater::repoSlug(Tool tool) const
{
    return tool == Tool::YtDlp ? QStringLiteral("yt-dlp/yt-dlp") : QStringLiteral("denoland/deno");
}

QString ToolsUpdater::assetName(Tool tool) const
{
    if (tool == Tool::YtDlp) {
#ifdef Q_OS_WIN
        return QStringLiteral("yt-dlp.exe");
#else
        return QStringLiteral("yt-dlp_macos"); // universal2
#endif
    }
#ifdef Q_OS_WIN
    // x86_64 tambien corre en Windows ARM por emulacion.
    return QStringLiteral("deno-x86_64-pc-windows-msvc.zip");
#else
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    return (arch.contains(QLatin1String("arm")) || arch.contains(QLatin1String("aarch64")))
               ? QStringLiteral("deno-aarch64-apple-darwin.zip")
               : QStringLiteral("deno-x86_64-apple-darwin.zip");
#endif
}

QString ToolsUpdater::sumsFileName(Tool tool) const
{
    return tool == Tool::YtDlp ? QStringLiteral("SHA2-256SUMS") : assetName(tool) + QStringLiteral(".sha256sum");
}

QUrl ToolsUpdater::githubUrl(const QString &path) const
{
    return QUrl(m_githubBase + path);
}

void ToolsUpdater::start()
{
    if (m_running) {
        return;
    }
#if !defined(Q_OS_WIN) && !defined(Q_OS_MAC)
    emit logMessage(QStringLiteral("Automatic tools install is not available on this platform"));
    emit finished(true);
    return;
#else
    if (!QDir().mkpath(stagingDir())) {
        emit logMessage(QStringLiteral("Tools: could not create %1").arg(QDir::toNativeSeparators(stagingDir())));
        emit finished(false);
        return;
    }
    m_running = true;
    m_allOk = true;
    m_pending = {Tool::YtDlp, Tool::Deno};
    emit runningChanged(true);
    qDebug() << "[ToolsUpdater] Inicio, base GitHub:" << m_githubBase << "carpeta:" << toolsDir();
    nextTool();
#endif
}

void ToolsUpdater::nextTool()
{
    discardDownload();
    m_phase = Phase::Idle;
    if (m_pending.isEmpty()) {
        m_running = false;
        emit runningChanged(false);
        emit finished(m_allOk);
        return;
    }
    startTool(m_pending.takeFirst());
}

void ToolsUpdater::failTool(const QString &line)
{
    m_allOk = false;
    qWarning() << "[ToolsUpdater]" << line;
    emit logMessage(line);
    // Siempre diferido: failTool se llama desde handlers de reply/proceso y arrancar la
    // siguiente tool ahi mismo anidaria requests dentro de la senal que se esta atendiendo.
    QTimer::singleShot(0, this, &ToolsUpdater::nextTool);
}

void ToolsUpdater::startTool(Tool tool)
{
    m_tool = tool;
    m_tag.clear();
    m_version.clear();
    m_expectedSha.clear();
    m_phase = Phase::ResolveTag;

    QNetworkRequest request(githubUrl(QStringLiteral("/%1/releases/latest").arg(repoSlug(tool))));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LGA_VideoDownloader/%1").arg(QCoreApplication::applicationVersion()));
    // Sin seguir el redirect: lo que interesa es el Location, que trae el tag.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(kMetaTimeoutMs);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &ToolsUpdater::onResolveFinished);
}

void ToolsUpdater::onResolveFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply) {
        return;
    }
    reply->deleteLater();
    const QString key = toolKey(m_tool);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    QUrl target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (target.isEmpty() && reply->hasRawHeader("Location")) {
        target = QUrl(QString::fromUtf8(reply->rawHeader("Location")));
    }
    if (reply->error() != QNetworkReply::NoError || (status != 301 && status != 302) || target.isEmpty()) {
        failTool(QStringLiteral("%1: could not check for updates (%2)")
                     .arg(key, reply->error() != QNetworkReply::NoError
                                   ? reply->errorString()
                                   : QStringLiteral("HTTP %1").arg(status)));
        return;
    }

    const QString path = reply->url().resolved(target).path();
    const QString marker = QStringLiteral("/releases/tag/");
    const int idx = path.indexOf(marker);
    const QString tag = idx >= 0 ? path.mid(idx + marker.size()) : QString();
    static const QRegularExpression tagRe(QStringLiteral("^[A-Za-z0-9._+-]{1,64}$"));
    if (!tagRe.match(tag).hasMatch()) {
        failTool(QStringLiteral("%1: unexpected release location, nothing installed").arg(key));
        return;
    }

    m_tag = tag;
    m_version = (m_tool == Tool::Deno && tag.startsWith(QLatin1Char('v'))) ? tag.mid(1) : tag;
    QVersionNumber remote;
    if (!parseVersion(m_version, &remote)) {
        failTool(QStringLiteral("%1: unrecognized release tag '%2', nothing installed").arg(key, tag));
        return;
    }
    qDebug() << "[ToolsUpdater] Tag resuelto" << key << "=" << tag;
    emit logMessage(QStringLiteral("%1: latest release is %2").arg(key, tag));

    if (m_tool == Tool::Deno && remote.majorVersion() != kDenoMajor) {
        emit logMessage(QStringLiteral("deno %1 is a new major version; keeping deno %2.x")
                            .arg(m_version).arg(kDenoMajor));
        QTimer::singleShot(0, this, &ToolsUpdater::nextTool);
        return;
    }

    QVersionNumber local;
    const QString localText = installedVersion(m_tool);
    if (!localText.isEmpty() && parseVersion(localText, &local)
        && QVersionNumber::compare(remote, local) <= 0) {
        emit logMessage(QStringLiteral("%1 is up to date (%2)").arg(key, localText));
        QTimer::singleShot(0, this, &ToolsUpdater::nextTool);
        return;
    }

    const QJsonObject stagedEntry = readState().value(QStringLiteral("staged")).toObject().value(key).toObject();
    if (stagedEntry.value(QStringLiteral("version")).toString() == m_version
        && QFileInfo(stagingDir() + QLatin1Char('/') + binaryName(m_tool)).isFile()) {
        emit logMessage(QStringLiteral("%1 %2 is already downloaded and verified").arg(key, m_version));
        emit toolStaged(key, m_version);
        QTimer::singleShot(0, this, &ToolsUpdater::nextTool);
        return;
    }

    m_phase = Phase::Sums;
    QNetworkRequest request(githubUrl(QStringLiteral("/%1/releases/download/%2/%3")
                                          .arg(repoSlug(m_tool), m_tag, sumsFileName(m_tool))));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LGA_VideoDownloader/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(kMetaTimeoutMs);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &ToolsUpdater::onSumsFinished);
}

void ToolsUpdater::onSumsFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply) {
        return;
    }
    reply->deleteLater();
    const QString key = toolKey(m_tool);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError || status != 200) {
        failTool(QStringLiteral("%1: checksum file not available (%2); nothing installed")
                     .arg(key, reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                                        : QStringLiteral("HTTP %1").arg(status)));
        return;
    }
    if (reply->bytesAvailable() > kMaxSumsBytes) {
        failTool(QStringLiteral("%1: checksum file too large; nothing installed").arg(key));
        return;
    }
    const QString sha = parseSha256(reply->readAll(), m_tool == Tool::YtDlp ? assetName(m_tool) : QString());
    if (sha.isEmpty()) {
        failTool(QStringLiteral("%1: no valid SHA-256 for %2; nothing installed").arg(key, assetName(m_tool)));
        return;
    }
    m_expectedSha = sha;

    if (!QDir().mkpath(downloadDir())) {
        failTool(QStringLiteral("%1: could not create the download folder").arg(key));
        return;
    }
    m_downloadPath = downloadDir() + QLatin1Char('/') + assetName(m_tool);
    m_file = new QSaveFile(m_downloadPath);
    if (!m_file->open(QIODevice::WriteOnly)) {
        failTool(QStringLiteral("%1: could not write %2 (%3)").arg(key, m_downloadPath, m_file->errorString()));
        return;
    }
    m_hash = new QCryptographicHash(QCryptographicHash::Sha256);
    m_statusChecked = false;
    m_writeFailed = false;
    m_phase = Phase::Asset;

    emit logMessage(QStringLiteral("%1: downloading %2...").arg(key, m_version));
    QNetworkRequest request(githubUrl(QStringLiteral("/%1/releases/download/%2/%3")
                                          .arg(repoSlug(m_tool), m_tag, assetName(m_tool))));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LGA_VideoDownloader/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(kAssetIdleTimeoutMs);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &ToolsUpdater::onAssetReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ToolsUpdater::onAssetFinished);
}

void ToolsUpdater::onAssetReadyRead()
{
    if (!m_reply || !m_file || !m_hash) {
        return;
    }
    // Status validado al primer dato: el cuerpo de un 404 no puede llegar al disco.
    if (!m_statusChecked) {
        m_statusChecked = true;
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            m_writeFailed = true;
            QTimer::singleShot(0, this, [this]() {
                if (m_reply) {
                    m_reply->abort();
                }
            });
            return;
        }
    }
    if (m_writeFailed) {
        return;
    }
    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty()) {
        return;
    }
    if (m_file->write(chunk) != chunk.size()) {
        m_writeFailed = true;
        QTimer::singleShot(0, this, [this]() {
            if (m_reply) {
                m_reply->abort();
            }
        });
        return;
    }
    m_hash->addData(chunk);
}

void ToolsUpdater::onAssetFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply) {
        return;
    }
    reply->deleteLater();
    const QString key = toolKey(m_tool);

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!m_writeFailed && m_file && reply->bytesAvailable() > 0) {
        if (!m_statusChecked) {
            m_statusChecked = true;
            m_writeFailed = status != 200;
        }
        if (!m_writeFailed) {
            const QByteArray tail = reply->readAll();
            if (m_file->write(tail) != tail.size()) {
                m_writeFailed = true;
            } else {
                m_hash->addData(tail);
            }
        }
    }

    if (reply->error() != QNetworkReply::NoError || status != 200 || m_writeFailed || !m_file) {
        const QString detail = m_writeFailed && status == 200
                                   ? QStringLiteral("write error")
                                   : (reply->error() != QNetworkReply::NoError && status == 200
                                          ? reply->errorString()
                                          : QStringLiteral("HTTP %1").arg(status));
        failTool(QStringLiteral("%1: download failed (%2); keeping the current version").arg(key, detail));
        return;
    }

    const QString got = QString::fromLatin1(m_hash->result().toHex());
    if (got.compare(m_expectedSha, Qt::CaseInsensitive) != 0) {
        qWarning() << "[ToolsUpdater] Hash distinto" << key << "esperado" << m_expectedSha << "obtenido" << got;
        failTool(QStringLiteral("%1: downloaded file failed integrity verification; keeping the current version").arg(key));
        return;
    }
    if (!m_file->commit()) {
        failTool(QStringLiteral("%1: could not save the download (%2)").arg(key, m_file->errorString()));
        return;
    }
    delete m_file;
    m_file = nullptr;
    delete m_hash;
    m_hash = nullptr;
    qDebug() << "[ToolsUpdater] Descarga verificada" << key << m_downloadPath;

    if (m_tool == Tool::Deno) {
        startExtract();
        return;
    }

    const QString stagedPath = stagingDir() + QLatin1Char('/') + binaryName(m_tool);
    QFile::remove(stagedPath);
    if (!renameWithRetry(m_downloadPath, stagedPath)) {
        failTool(QStringLiteral("%1: could not stage the new binary").arg(key));
        return;
    }
    makeExecutable(stagedPath);
    startSmokeTest();
}

void ToolsUpdater::startExtract()
{
    m_phase = Phase::Extract;
    QDir(extractDir()).removeRecursively();
    if (!QDir().mkpath(extractDir())) {
        failTool(QStringLiteral("deno: could not create the extraction folder"));
        return;
    }

    QString program;
    QStringList args;
#ifdef Q_OS_WIN
    // bsdtar viene con Windows 10 1803+ y abre .zip.
    program = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"))).filePath(QStringLiteral("System32/tar.exe"));
    if (!QFileInfo::exists(program)) {
        program = QStringLiteral("tar");
    }
    // tar.exe lee sus argumentos en el code page ANSI: una ruta absoluta con caracteres fuera
    // de ese code page (p. ej. un usuario "日本" u "Ω") le llega rota. Se lo corre con la
    // carpeta de extraccion como working directory (QProcess la pasa en UTF-16) y solo
    // rutas relativas ASCII: el zip vive en .staging/dl y se extrae en .staging/extract.
    args << QStringLiteral("-xf") << (QStringLiteral("../dl/") + QFileInfo(m_downloadPath).fileName()) << QStringLiteral("-C")
         << QStringLiteral(".") << binaryName(Tool::Deno);
#else
    program = QStringLiteral("/usr/bin/ditto");
    args << QStringLiteral("-x") << QStringLiteral("-k") << m_downloadPath << extractDir();
#endif

    QProcess *process = new QProcess(this);
    m_process = process;
#ifdef Q_OS_WIN
    process->setWorkingDirectory(extractDir());
#endif
    QTimer *timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, [process]() { process->kill(); });
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        process->deleteLater();
        const QString extracted = extractDir() + QLatin1Char('/') + binaryName(Tool::Deno);
        if (exitStatus != QProcess::NormalExit || exitCode != 0 || !QFileInfo(extracted).isFile()) {
            qWarning() << "[ToolsUpdater] Extraccion fallida:" << process->readAllStandardError();
            failTool(QStringLiteral("deno: could not extract the downloaded archive; keeping the current version"));
            return;
        }
        const QString stagedPath = stagingDir() + QLatin1Char('/') + binaryName(Tool::Deno);
        QFile::remove(stagedPath);
        if (!renameWithRetry(extracted, stagedPath)) {
            failTool(QStringLiteral("deno: could not stage the new binary"));
            return;
        }
        QFile::remove(m_downloadPath);
        QDir(extractDir()).removeRecursively();
        makeExecutable(stagedPath);
        startSmokeTest();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            failTool(QStringLiteral("deno: could not start the archive extractor"));
        }
    });
    timer->start(kExtractTimeoutMs);
    process->start(program, args);
}

void ToolsUpdater::startSmokeTest()
{
    m_phase = Phase::Smoke;
    const QString key = toolKey(m_tool);
    const QString stagedPath = stagingDir() + QLatin1Char('/') + binaryName(m_tool);

    QProcess *process = new QProcess(this);
    m_process = process;
    QTimer *timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, [process]() { process->kill(); });
    connect(process, &QProcess::finished, this, [this, process, key, stagedPath](int exitCode, QProcess::ExitStatus exitStatus) {
        process->deleteLater();
        const QString output = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            qWarning() << "[ToolsUpdater] Smoke test fallido" << key << exitCode << output;
            QFile::remove(stagedPath);
            failTool(QStringLiteral("%1: the downloaded binary did not run; keeping the current version").arg(key));
            return;
        }
        qDebug() << "[ToolsUpdater] Smoke test OK" << key << output.section(QLatin1Char('\n'), 0, 0);
        markStaged();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, key, stagedPath](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            QFile::remove(stagedPath);
            failTool(QStringLiteral("%1: the downloaded binary could not start; keeping the current version").arg(key));
        }
    });
    timer->start(kSmokeTimeoutMs);
    process->start(stagedPath, {QStringLiteral("--version")});
}

void ToolsUpdater::markStaged()
{
    const QString key = toolKey(m_tool);
    QJsonObject state = readState();
    QJsonObject staged = state.value(QStringLiteral("staged")).toObject();
    QJsonObject entry;
    entry.insert(QStringLiteral("version"), m_version);
    entry.insert(QStringLiteral("sha256"), m_expectedSha);
    entry.insert(QStringLiteral("asset"), assetName(m_tool));
    staged.insert(key, entry);
    state.insert(QStringLiteral("staged"), staged);
    if (!writeState(state)) {
        QFile::remove(stagingDir() + QLatin1Char('/') + binaryName(m_tool));
        failTool(QStringLiteral("%1: could not record the new version").arg(key));
        return;
    }
    emit logMessage(QStringLiteral("%1 %2 downloaded and verified").arg(key, m_version));
    emit toolStaged(key, m_version);
    QTimer::singleShot(0, this, &ToolsUpdater::nextTool);
}

void ToolsUpdater::discardDownload()
{
    if (m_file) {
        m_file->cancelWriting(); // borra el temporal: un parcial nunca queda en disco
        delete m_file;
        m_file = nullptr;
    }
    delete m_hash;
    m_hash = nullptr;
    if (!m_downloadPath.isEmpty() && m_tool == Tool::Deno) {
        QFile::remove(m_downloadPath);
    }
    m_downloadPath.clear();
    m_statusChecked = false;
    m_writeFailed = false;
}

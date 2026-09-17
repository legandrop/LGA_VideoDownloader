#include "videodownloader/downloadqueue.h"
#include "videodownloader/browserdetect.h"
#include "videodownloader/toolsmanager.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

namespace {

// Pausa entre una descarga y la siguiente: deja que el proceso anterior libere archivos.
constexpr int NEXT_DOWNLOAD_DELAY_MS = 300;
// Minimo entre dos avisos de progreso del mismo item: la UI no necesita mas de ~7 por segundo.
constexpr int PROGRESS_EMIT_INTERVAL_MS = 150;

// Marcadores de las lineas estructuradas que se le piden a yt-dlp con --progress-template
// y --print. Se reconocen con startsWith: nada de regex por linea.
const QLatin1String kProgressTag("[vdprog] ");
const QLatin1String kInfoTag("[vdinfo] ");
const QLatin1String kFileTag("[vdfile] ");

qint64 parseBytes(const QStringView &text)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok ? static_cast<qint64>(value) : -1;
}

QString humanSize(qint64 bytes)
{
    if (bytes < 0) {
        return QString();
    }
    const double mib = bytes / (1024.0 * 1024.0);
    if (mib >= 1024.0) {
        return QString::number(mib / 1024.0, 'f', 2) + QStringLiteral(" GiB");
    }
    return QString::number(mib, 'f', mib >= 100 ? 0 : 1) + QStringLiteral(" MiB");
}

// Ultima linea "ERROR: ..." de stderr, sin el prefijo del extractor ("[youtube] id: ").
QString lastErrorLine(const QString &err)
{
    const QStringList lines = err.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        QString line = lines.at(i).trimmed();
        if (!line.startsWith(QLatin1String("ERROR:"))) {
            continue;
        }
        line = line.mid(6).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            const int close = line.indexOf(QLatin1String("]"));
            const int colon = line.indexOf(QLatin1String(": "), close);
            if (close > 0 && colon > close) {
                line = line.mid(colon + 2);
            }
        }
        return line;
    }
    return QString();
}

} // namespace

DownloadQueue::DownloadQueue(ToolsManager *toolsManager, QObject *parent)
    : QObject(parent)
    , m_toolsManager(toolsManager)
{
}

DownloadQueue::~DownloadQueue()
{
    if (m_currentProcess) {
        m_currentProcess->disconnect(this);
    }
    cleanupCurrentProcess();
}

int DownloadQueue::addDownload(const QString &url, const DownloadOptions &options)
{
    DownloadItem item;
    item.id = m_nextId++;
    item.url = url;
    item.options = options;
    m_items.append(item);
    emit itemAdded(item);

    m_stopped = false;
    if (m_currentId < 0) {
        QTimer::singleShot(0, this, &DownloadQueue::processNextDownload);
    }
    return item.id;
}

int DownloadQueue::addInvalidLink(const QString &text)
{
    DownloadItem item;
    item.id = m_nextId++;
    item.url = text;
    item.status = DownloadStatus::Failed;
    item.failure = FailureKind::InvalidLink;
    item.errorHeadline = QStringLiteral("Not a Vimeo or YouTube link");
    item.errorDetail = QStringLiteral("Check the link, paste it again and press Download.");
    item.finishTime = QDateTime::currentDateTime();
    m_items.append(item);
    emit itemAdded(item);
    log(QStringLiteral("Skipped \"%1\": not a Vimeo or YouTube link").arg(text), LogLevel::Warning);
    return item.id;
}

DownloadItem *DownloadQueue::findItem(int id)
{
    const int index = indexOf(id);
    return index >= 0 ? &m_items[index] : nullptr;
}

const DownloadItem *DownloadQueue::item(int id) const
{
    const int index = indexOf(id);
    return index >= 0 ? &m_items.at(index) : nullptr;
}

int DownloadQueue::indexOf(int id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

DownloadItem *DownloadQueue::currentItem()
{
    return m_currentId >= 0 ? findItem(m_currentId) : nullptr;
}

void DownloadQueue::cancelItem(int id)
{
    DownloadItem *target = findItem(id);
    if (!target || target->isFinished()) {
        return;
    }
    if (id == m_currentId) {
        // El cierre real llega por onDownloadFinished, que ve el estado Cancelled.
        target->status = DownloadStatus::Cancelled;
        log(QStringLiteral("Cancelling %1").arg(target->title.isEmpty() ? target->url : target->title));
        killCurrentProcessTree();
        return;
    }
    target->status = DownloadStatus::Cancelled;
    target->finishTime = QDateTime::currentDateTime();
    emit itemUpdated(*target);
}

void DownloadQueue::removeItem(int id)
{
    if (id == m_currentId) {
        return;
    }
    const int index = indexOf(id);
    if (index < 0) {
        return;
    }
    m_items.removeAt(index);
    emit itemRemoved(id);
}

void DownloadQueue::retryItem(int id, const DownloadOptions &options)
{
    DownloadItem *target = findItem(id);
    if (!target || !target->isFinished() || target->failure == FailureKind::InvalidLink
        || target->status == DownloadStatus::Completed) {
        return;
    }
    const QString url = target->url;
    const QString title = target->title;
    const QString password = target->videoPassword;
    *target = DownloadItem();
    target->id = id;
    target->url = url;
    target->title = title;
    target->videoPassword = password;
    target->options = options;
    emit itemUpdated(*target);
    log(QStringLiteral("Retrying %1").arg(title.isEmpty() ? url : title));

    m_stopped = false;
    if (m_currentId < 0) {
        QTimer::singleShot(0, this, &DownloadQueue::processNextDownload);
    }
}

void DownloadQueue::retryFailed(const DownloadOptions &options)
{
    QList<int> ids;
    for (const DownloadItem &entry : std::as_const(m_items)) {
        if (entry.status == DownloadStatus::Failed && entry.failure != FailureKind::InvalidLink) {
            ids.append(entry.id);
        }
    }
    for (int id : std::as_const(ids)) {
        retryItem(id, options);
    }
}

void DownloadQueue::clearFinished()
{
    QList<int> ids;
    for (const DownloadItem &entry : std::as_const(m_items)) {
        if (entry.isFinished()) {
            ids.append(entry.id);
        }
    }
    for (int id : std::as_const(ids)) {
        removeItem(id);
    }
}

void DownloadQueue::cancelAll()
{
    for (DownloadItem &entry : m_items) {
        if (entry.status == DownloadStatus::Pending) {
            entry.status = DownloadStatus::Cancelled;
            entry.finishTime = QDateTime::currentDateTime();
            emit itemUpdated(entry);
        }
    }
    if (m_currentId >= 0) {
        cancelItem(m_currentId);
    }
}

void DownloadQueue::kick()
{
    if (m_currentId < 0 && !m_stopped) {
        processNextDownload();
    }
}

void DownloadQueue::processNextDownload()
{
    if (m_currentId >= 0 || m_stopped) {
        return;
    }

    DownloadItem *next = nullptr;
    for (DownloadItem &entry : m_items) {
        if (entry.status == DownloadStatus::Pending) {
            next = &entry;
            break;
        }
    }
    if (!next) {
        return;
    }

    // Sin tools no se lanza nada: los links quedan en cola y arrancan con kick() cuando
    // el auto-update termina de instalarlas.
    if (m_toolsManager && !m_toolsManager->areToolsInstalled()) {
        if (!m_waitingForTools) {
            m_waitingForTools = true;
            log(QStringLiteral("Waiting for the download tools to finish installing"), LogLevel::Warning);
        }
        return;
    }
    m_waitingForTools = false;

    m_currentId = next->id;
    next->status = DownloadStatus::Downloading;
    next->startTime = QDateTime::currentDateTime();
    emit itemUpdated(*next);
    startDownloadProcess(*next);
}

bool DownloadQueue::hasActiveProcess() const
{
    return m_currentProcess && m_currentProcess->state() != QProcess::NotRunning;
}

int DownloadQueue::activeDownloadCount() const
{
    int count = 0;
    for (const DownloadItem &entry : m_items) {
        if (entry.status == DownloadStatus::Pending || entry.status == DownloadStatus::Downloading) {
            ++count;
        }
    }
    return count;
}

void DownloadQueue::stopAllForShutdown()
{
    m_stopped = true;
    for (DownloadItem &entry : m_items) {
        if (entry.status == DownloadStatus::Pending || entry.status == DownloadStatus::Downloading) {
            entry.status = DownloadStatus::Cancelled;
        }
    }
    if (m_currentProcess) {
        // Sin senales: que no dispare onDownloadFinished ni encadene la proxima descarga.
        m_currentProcess->disconnect(this);
    }
    cleanupCurrentProcess();
    m_currentId = -1;
    log(QStringLiteral("Downloads stopped to install an update"), LogLevel::Warning);
}

void DownloadQueue::startDownloadProcess(DownloadItem &item)
{
    cleanupCurrentProcess();
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_streamCount = 1;
    m_streamIndex = -1;
    m_streamDoneBase = 0;
    m_lastStreamTotal = 0;
    m_progressThrottle.invalidate();

    // Punto unico de swap de tools: aca no queda ningun yt-dlp de esta cola corriendo, asi
    // que si el auto-update dejo algo verificado en staging se activa antes de usarlo.
    if (m_toolsManager) {
        m_toolsManager->applyStagedTools();
    }

    m_currentProcess = new QProcess(this);
#ifdef Q_OS_UNIX
    // Grupo de procesos propio: asi killCurrentProcessTree() alcanza a los hijos de yt-dlp.
    m_currentProcess->setChildProcessModifier([]() { ::setpgid(0, 0); });
#endif
    // Sin esto, en Windows yt-dlp escribe los titulos en la codificacion de la consola.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    m_currentProcess->setProcessEnvironment(env);

    connect(m_currentProcess, &QProcess::readyReadStandardOutput, this, &DownloadQueue::onDownloadOutput);
    connect(m_currentProcess, &QProcess::readyReadStandardError, this, &DownloadQueue::onDownloadError);
    connect(m_currentProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &DownloadQueue::onDownloadFinished);

    QStringList arguments;

    // Autenticacion: cookies de una sesion ya iniciada en el navegador, o un cookies.txt.
    // No se usa -u/-p: pedirle a cada usuario el mail y la contrasena de su cuenta es
    // inseguro, y ademas YouTube no acepta login por contrasena desde yt-dlp.
    if (!item.options.cookiesFile.isEmpty()) {
        arguments << "--cookies" << item.options.cookiesFile;
    } else if (!item.options.cookiesBrowser.isEmpty()) {
        arguments << "--cookies-from-browser" << item.options.cookiesBrowser;
    }
    if (!item.videoPassword.isEmpty()) {
        arguments << "--video-password" << item.videoPassword;
    }

    arguments << "--output" << item.options.downloadDir + "/%(title).200s.%(ext)s";
    arguments << "--restrict-filenames";

    // Salida estructurada para la tarjeta: progreso en bytes, datos del formato elegido y
    // ruta final. Con --print yt-dlp pasa a modo silencioso y simulado; --no-quiet y
    // --no-simulate lo devuelven al modo normal.
    arguments << "--newline" << "--no-quiet" << "--no-simulate" << "--progress" << "--no-colors";
    arguments << "--progress-template"
              << "download:[vdprog] %(progress.downloaded_bytes)s|%(progress.total_bytes)s|"
                 "%(progress.total_bytes_estimate)s|%(progress.speed)s|%(progress.eta)s";
    arguments << "--print" << "before_dl:[vdinfo] %(format_id)s|%(resolution)s|%(ext)s|%(title)s";
    arguments << "--print" << "after_move:[vdfile] %(filepath)s";

    if (item.options.format == OutputFormat::AudioM4a) {
        arguments << "--format" << "ba[ext=m4a]/ba/b" << "--extract-audio" << "--audio-format" << "m4a";
    } else if (item.options.quality == VideoQuality::Compatible) {
        // H.264 + AAC primero aunque haya mas resolucion en AV1/VP9: abre en cualquier editor.
        arguments << "--format-sort" << "vcodec:h264,res,acodec:aac" << "--merge-output-format" << "mp4";
    } else {
        arguments << "--format-sort" << "res" << "--merge-output-format" << "mp4" << "--remux-video" << "mp4";
    }

    const QString ffmpegPath = m_toolsManager ? m_toolsManager->getFfmpegPath() : QString();
    if (!ffmpegPath.isEmpty() && ffmpegPath != QLatin1String("ffmpeg")) {
        arguments << "--ffmpeg-location" << ffmpegPath;
    } else {
        log(QStringLiteral("WARNING: ffmpeg path not found, using the system ffmpeg"), LogLevel::Warning);
    }

    // YouTube exige un runtime de JavaScript para resolver sus desafios (firma y "n").
    // yt-dlp solo busca deno en el PATH por defecto, asi que se le pasa la ruta explicita
    // del deno que distribuye la app.
    if (item.isYouTube()) {
        if (m_toolsManager && m_toolsManager->isDenoInstalled()) {
            const QString denoPath = m_toolsManager->getDenoPath();
            if (!denoPath.isEmpty()) {
                arguments << "--js-runtimes" << QStringLiteral("deno:%1").arg(denoPath);
            }
        } else {
            log(QStringLiteral("WARNING: Deno was not found; YouTube downloads may fail or miss formats"),
                LogLevel::Warning);
        }
    }

    arguments << item.url;

    QString login = QStringLiteral("no browser session");
    if (!item.options.cookiesFile.isEmpty()) {
        login = QStringLiteral("cookies file %1").arg(QDir::toNativeSeparators(item.options.cookiesFile));
    } else if (!item.options.cookiesBrowser.isEmpty()) {
        login = QStringLiteral("the %1 session").arg(BrowserDetect::displayName(item.options.cookiesBrowser));
    }
    log(QStringLiteral("Fetching info · %1 · cookies: %2").arg(item.url, login));

    const QString ytDlpPath = m_toolsManager ? m_toolsManager->getYtDlpPath() : QStringLiteral("yt-dlp");
    QString commandLog = arguments.join(QLatin1Char(' '));
    // La contrasena de VIDEO no debe quedar en el log.
    if (!item.videoPassword.isEmpty()) {
        commandLog.replace(item.videoPassword, QStringLiteral("***"));
    }
    log(QStringLiteral("Command: %1 %2").arg(QDir::toNativeSeparators(ytDlpPath), commandLog), LogLevel::Detail);
    m_currentProcess->start(ytDlpPath, arguments);

    if (!m_currentProcess->waitForStarted(5000)) {
        item.errorMessage = QStringLiteral("ERROR: Could not start yt-dlp");
        item.status = DownloadStatus::Failed;
        onDownloadFinished(-1, QProcess::CrashExit);
    }
}

void DownloadQueue::onDownloadOutput()
{
    if (!m_currentProcess) {
        return;
    }
    m_stdoutBuffer += QString::fromUtf8(m_currentProcess->readAllStandardOutput());
    int newline;
    while ((newline = m_stdoutBuffer.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (!line.isEmpty()) {
            handleStdoutLine(line);
        }
    }
}

void DownloadQueue::handleStdoutLine(const QString &line)
{
    DownloadItem *item = currentItem();
    if (!item) {
        return;
    }

    if (line.startsWith(kProgressTag)) {
        // downloaded|total|estimate|speed|eta ; "NA" cuando yt-dlp todavia no lo sabe.
        const QList<QStringView> parts = QStringView(line).mid(kProgressTag.size()).split(QLatin1Char('|'));
        if (parts.size() < 5) {
            return;
        }
        const qint64 done = parseBytes(parts.at(0));
        qint64 total = parseBytes(parts.at(1));
        if (total <= 0) {
            total = parseBytes(parts.at(2));
        }
        if (m_streamIndex < 0) {
            m_streamIndex = 0;
        }
        if (total > 0) {
            m_lastStreamTotal = total;
        }
        if (done >= 0) {
            item->doneBytes = m_streamDoneBase + done;
        }
        item->totalBytes = m_streamDoneBase + qMax<qint64>(total, 0);
        bool ok = false;
        const double speed = parts.at(3).toDouble(&ok);
        item->speedBytes = ok ? speed : -1;
        const int eta = parts.at(4).toInt(&ok);
        item->etaSeconds = ok ? eta : -1;
        if (done >= 0 && total > 0) {
            const double fraction = qBound(0.0, double(done) / double(total), 1.0);
            const int overall = int(((m_streamIndex + fraction) / qMax(1, m_streamCount)) * 100.0);
            // Monotono: un stream nuevo no hace retroceder la barra.
            item->progress = qBound(item->progress, overall, 100);
        }
        emitUpdated(*item, true);
        return;
    }

    if (line.startsWith(kInfoTag)) {
        // format_id|resolution|ext|title (el titulo va al final porque puede traer '|').
        const QString rest = line.mid(kInfoTag.size());
        const QStringList parts = rest.split(QLatin1Char('|'));
        if (parts.size() >= 4) {
            m_streamCount = parts.at(0).count(QLatin1Char('+')) + 1;
            const QString resolution = parts.at(1);
            item->resolution = resolution == QLatin1String("audio only") || resolution == QLatin1String("NA")
                                   ? QString() : resolution;
            item->extension = item->options.format == OutputFormat::AudioM4a ? QStringLiteral("m4a") : parts.at(2);
            item->title = parts.mid(3).join(QLatin1Char('|'));
            log(QStringLiteral("Format: %1 %2 · %3").arg(item->resolution.isEmpty() ? QStringLiteral("audio") : item->resolution,
                                                          item->extension, item->title));
            emitUpdated(*item);
        }
        return;
    }

    if (line.startsWith(kFileTag)) {
        item->filePath = line.mid(kFileTag.size()).trimmed();
        return;
    }

    if (line.startsWith(QLatin1String("[download] Destination:"))) {
        // Cada stream (video, audio) arranca con su propio Destination.
        if (m_streamIndex >= 0) {
            m_streamDoneBase += m_lastStreamTotal;
            m_lastStreamTotal = 0;
        }
        m_streamIndex = qMin(m_streamIndex + 1, qMax(0, m_streamCount - 1));
    } else if (line.startsWith(QLatin1String("[Merger]")) || line.startsWith(QLatin1String("[ExtractAudio]"))
               || line.startsWith(QLatin1String("[VideoRemuxer]")) || line.startsWith(QLatin1String("[FixupM3u8]"))) {
        item->finishing = true;
        item->progress = 100;
        item->speedBytes = -1;
        item->etaSeconds = -1;
        emitUpdated(*item);
    }

    log(line, classifyLogLine(line));
}

void DownloadQueue::onDownloadError()
{
    if (!m_currentProcess) {
        return;
    }
    // stderr de yt-dlp NO es solo errores: ahi van tambien los WARNING. Se loguea linea por
    // linea tal cual; el fragmento sin salto de linea final se guarda hasta el proximo chunk.
    m_stderrBuffer += QString::fromUtf8(m_currentProcess->readAllStandardError());
    int newline;
    while ((newline = m_stderrBuffer.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = m_stderrBuffer.left(newline).trimmed();
        m_stderrBuffer.remove(0, newline + 1);
        if (!line.isEmpty()) {
            log(line, classifyLogLine(line));
            if (DownloadItem *item = currentItem()) {
                item->errorMessage += line + QLatin1Char('\n');
            }
        }
    }
}

void DownloadQueue::flushStderrBuffer()
{
    const QString rest = m_stderrBuffer.trimmed();
    m_stderrBuffer.clear();
    if (!rest.isEmpty()) {
        log(rest, classifyLogLine(rest));
        if (DownloadItem *item = currentItem()) {
            item->errorMessage += rest + QLatin1Char('\n');
        }
    }
    const QString out = m_stdoutBuffer.trimmed();
    m_stdoutBuffer.clear();
    if (!out.isEmpty()) {
        handleStdoutLine(out);
    }
}

void DownloadQueue::classifyFailure(DownloadItem &item) const
{
    // Traduce los errores conocidos de yt-dlp a un titular y una solucion para la tarjeta.
    // Los textos se comparan contra la salida real de yt-dlp 2026.08.
    const QString &err = item.errorMessage;
    const bool usedCookies = !item.options.cookiesBrowser.isEmpty() || !item.options.cookiesFile.isEmpty();
    const QString browser = BrowserDetect::displayName(item.options.cookiesBrowser);
    const QString site = item.isVimeo() ? QStringLiteral("Vimeo") : QStringLiteral("YouTube");
    const auto has = [&err](const char *text) { return err.contains(QLatin1String(text), Qt::CaseInsensitive); };

    if (has("Could not copy Chrome cookie database")) {
        item.failure = FailureKind::CookiesUnreadable;
        item.errorHeadline = QStringLiteral("Close %1 to read its session").arg(browser);
        item.errorDetail = QStringLiteral("%1 locks its cookies while it is open. Close it completely (also from the "
                                          "system tray) and retry, or choose Firefox or a cookies.txt file.").arg(browser);
    } else if (has("Failed to decrypt with DPAPI") || has("app-bound")) {
        item.failure = FailureKind::CookiesUnreadable;
        item.errorHeadline = QStringLiteral("Can't read %1 cookies on Windows").arg(browser);
        item.errorDetail = QStringLiteral("%1 encrypts its cookies on Windows. Sign in to %2 in Firefox and choose "
                                          "Firefox in Use cookies from, or load a cookies.txt file.").arg(browser, site);
    } else if (has("could not find") && has("cookies database")) {
        item.failure = FailureKind::CookiesUnreadable;
        item.errorHeadline = QStringLiteral("No %1 session found").arg(browser);
        item.errorDetail = QStringLiteral("Open %1 at least once and sign in to %2, then retry.").arg(browser, site);
    } else if (has("Sign in to confirm") || has("only works when logged-in") || has("--cookies-from-browser or --cookies")
               || has("members-only") || has("Join this channel") || has("Private video") || has("This video is private")
               || has("logged-in") || has("login required")) {
        item.failure = FailureKind::NeedsSignIn;
        if (has("confirm your age")) {
            item.errorHeadline = QStringLiteral("Sign in to confirm your age");
        } else if (has("members-only") || has("Join this channel")) {
            item.errorHeadline = QStringLiteral("Members-only video");
        } else if (has("private")) {
            item.errorHeadline = QStringLiteral("This video is private");
        } else {
            item.errorHeadline = QStringLiteral("This video needs a signed-in account");
        }
        if (!usedCookies) {
#ifdef Q_OS_WIN
            item.errorDetail = QStringLiteral("%1 only shows this video to signed-in users. Sign in to %1 in Firefox, "
                                              "choose it in Use cookies from, and retry.").arg(site);
#else
            item.errorDetail = QStringLiteral("%1 only shows this video to signed-in users. Sign in to %1 in your "
                                              "browser, choose it in Use cookies from, and retry.").arg(site);
#endif
        } else {
            item.errorDetail = QStringLiteral("The selected session has no access to this video. Sign in to %1 with an "
                                              "account that can watch it (or export a fresh cookies.txt), then retry.").arg(site);
        }
    } else if (has("Unsupported URL") || has("is not a valid URL")) {
        item.failure = FailureKind::InvalidLink;
        item.errorHeadline = QStringLiteral("This link doesn't point to a video");
        item.errorDetail = QStringLiteral("Check the link, paste it again and press Download.");
    } else if (has("Video unavailable") || has("HTTP Error 404") || has("This video does not exist")) {
        item.failure = FailureKind::Unavailable;
        item.errorHeadline = QStringLiteral("Video unavailable");
        item.errorDetail = QStringLiteral("The video was removed or the link is wrong.");
    } else if (has("Unable to download webpage") || has("getaddrinfo failed") || has("timed out")
               || has("Connection reset") || has("Temporary failure in name resolution")) {
        item.failure = FailureKind::Network;
        item.errorHeadline = QStringLiteral("Connection problem");
        item.errorDetail = QStringLiteral("Check your internet connection and retry.");
    } else if (has("Could not start yt-dlp")) {
        item.failure = FailureKind::ToolsMissing;
        item.errorHeadline = QStringLiteral("The download tools aren't ready");
        item.errorDetail = QStringLiteral("They install automatically when the app starts. Restart the app and retry.");
    } else if (has("protected by a password") || has("--video-password")) {
        item.failure = FailureKind::PasswordRequired;
        item.errorHeadline = QStringLiteral("This video has a password");
        item.errorDetail = QStringLiteral("Retry and enter the video password when asked.");
    } else if (has("Requested format is not available")) {
        item.failure = FailureKind::Generic;
        item.errorHeadline = QStringLiteral("Format not available");
        item.errorDetail = QStringLiteral("Try Best quality, or retry later: the download tools update when the app starts.");
    } else {
        item.failure = FailureKind::Generic;
        item.errorHeadline = QStringLiteral("Download failed");
        const QString line = lastErrorLine(err);
        item.errorDetail = line.isEmpty() ? QStringLiteral("yt-dlp stopped with an error. See the log for details.") : line;
    }
}

void DownloadQueue::onDownloadFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    DownloadItem *item = currentItem();
    if (!item) {
        return;
    }

    onDownloadError();
    flushStderrBuffer();
    item = currentItem();
    if (!item) {
        return;
    }
    item->finishTime = QDateTime::currentDateTime();
    item->speedBytes = -1;
    item->etaSeconds = -1;
    item->finishing = false;

    if (item->status == DownloadStatus::Cancelled) {
        log(QStringLiteral("Cancelled %1").arg(item->title.isEmpty() ? item->url : item->title), LogLevel::Warning);
    } else if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        item->status = DownloadStatus::Completed;
        item->progress = 100;
        qint64 size = -1;
        if (!item->filePath.isEmpty()) {
            size = QFileInfo(item->filePath).size();
        }
        if (size > 0) {
            item->totalBytes = size;
        }
        const QString name = item->filePath.isEmpty() ? item->title : QFileInfo(item->filePath).fileName();
        log(size > 0 ? QStringLiteral("Saved %1 (%2)").arg(name, humanSize(size)) : QStringLiteral("Saved %1").arg(name),
            LogLevel::Done);
    } else {
        const bool passwordError = item->errorMessage.contains(QLatin1String("protected by a password"), Qt::CaseInsensitive)
                                   || item->errorMessage.contains(QLatin1String("--video-password"), Qt::CaseInsensitive);
        if (passwordError && item->videoPassword.isEmpty()) {
            // El item sigue siendo el actual hasta que el usuario responda el dialogo.
            log(QStringLiteral("This video needs a password"), LogLevel::Warning);
            cleanupCurrentProcess();
            emit videoPasswordRequired(*item);
            return;
        }
        item->status = DownloadStatus::Failed;
        classifyFailure(*item);
        log(QStringLiteral("%1 · %2").arg(item->errorHeadline, item->errorDetail), LogLevel::Error);
    }

    emit itemUpdated(*item);
    finishCurrent();
}

void DownloadQueue::finishCurrent()
{
    m_currentId = -1;
    cleanupCurrentProcess();
    QTimer::singleShot(NEXT_DOWNLOAD_DELAY_MS, this, &DownloadQueue::processNextDownload);
}

void DownloadQueue::retryDownloadWithVideoPassword(const QString &videoPassword)
{
    DownloadItem *item = currentItem();
    if (!item) {
        return;
    }
    item->videoPassword = videoPassword;
    item->errorMessage.clear();
    item->status = DownloadStatus::Downloading;
    item->startTime = QDateTime::currentDateTime();
    log(QStringLiteral("Retrying with the video password"));
    startDownloadProcess(*item);
}

void DownloadQueue::abandonPasswordRequest()
{
    DownloadItem *item = currentItem();
    if (!item) {
        return;
    }
    item->status = DownloadStatus::Failed;
    item->finishTime = QDateTime::currentDateTime();
    item->failure = FailureKind::PasswordRequired;
    item->errorHeadline = QStringLiteral("This video has a password");
    item->errorDetail = QStringLiteral("No password was entered. Retry and type the video password when asked.");
    log(QStringLiteral("Video password not provided"), LogLevel::Error);
    emit itemUpdated(*item);
    finishCurrent();
}

void DownloadQueue::log(const QString &text, LogLevel level)
{
    emit logLine(text, level);
}

void DownloadQueue::emitUpdated(const DownloadItem &item, bool throttle)
{
    if (throttle) {
        if (m_progressThrottle.isValid() && m_progressThrottle.elapsed() < PROGRESS_EMIT_INTERVAL_MS
            && item.progress < 100) {
            return;
        }
        m_progressThrottle.restart();
    }
    emit itemUpdated(item);
}

void DownloadQueue::killCurrentProcessTree()
{
    if (!m_currentProcess || m_currentProcess->state() == QProcess::NotRunning) {
        return;
    }
    const qint64 pid = m_currentProcess->processId();
#ifdef Q_OS_WIN
    if (pid > 0) {
        const QString taskkill = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows")))
                                     .filePath(QStringLiteral("System32/taskkill.exe"));
        QProcess::execute(taskkill, {QStringLiteral("/T"), QStringLiteral("/F"), QStringLiteral("/PID"),
                                     QString::number(pid)});
    }
#elif defined(Q_OS_UNIX)
    if (pid > 0) {
        ::kill(-static_cast<pid_t>(pid), SIGKILL); // grupo entero (setpgid al lanzar)
    }
#endif
    // Respaldo por si el arbol no se pudo matar (taskkill ausente, pid ya reciclado).
    if (m_currentProcess->state() != QProcess::NotRunning) {
        m_currentProcess->kill();
    }
}

void DownloadQueue::cleanupCurrentProcess()
{
    if (m_currentProcess) {
        if (m_currentProcess->state() != QProcess::NotRunning) {
            m_currentProcess->disconnect(this);
            killCurrentProcessTree();
            m_currentProcess->waitForFinished(3000);
        }
        m_currentProcess->deleteLater();
        m_currentProcess = nullptr;
    }
}

#include "videodownloader/downloadqueue.h"
#include "videodownloader/toolsmanager.h"

#include <QDir>
#include <QRegularExpression>
#include <QMutexLocker>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

DownloadQueue::DownloadQueue(QTextEdit *logOutput, QProgressBar *progressBar, QGroupBox *progressGroup, ToolsManager *toolsManager, QObject *parent)
    : QObject(parent)
    , m_logOutput(logOutput)
    , m_progressBar(progressBar)
    , m_progressGroup(progressGroup)
    , m_toolsManager(toolsManager)
    , m_currentProcess(nullptr)
    , m_isRunning(false)
    , m_isPaused(false)
    , m_completedCount(0)
    , m_totalCount(0)
    , m_hasCurrentDownload(false)
    , m_totalFragments(0)
    , m_currentFragment(0)
{
    updateProgressLabel();
}

DownloadQueue::~DownloadQueue()
{
    cleanupCurrentProcess();
}

void DownloadQueue::addDownload(const QString &url, const QString &cookiesBrowser, const QString &cookiesFile, const QString &downloadDir)
{
    QMutexLocker locker(&m_queueMutex);

    DownloadItem item(url, cookiesBrowser, cookiesFile, downloadDir);
    m_queue.enqueue(item);
    m_totalCount++;

    updateProgressLabel();

    logMessage(QString("=== Download Added to Queue ==="));
    logMessage(QString("URL: %1").arg(url));
    logMessage(QString("Queue position: %1 of %2").arg(m_queue.size()).arg(m_totalCount));
    logMessage("---");

    // Emit signal for total count update, but don't change current number
    emit downloadAddedToQueue(m_totalCount);

    // Auto-start queue if not running
    if (!m_isRunning && !m_isPaused) {
        QTimer::singleShot(100, this, &DownloadQueue::startQueue);
    }
}

void DownloadQueue::retryDownloadWithVideoPassword(const QString &videoPassword)
{
    if (!m_hasCurrentDownload) return;

    // Set the video password and retry the download
    m_currentDownload.videoPassword = videoPassword;
    m_currentDownload.errorMessage.clear();
    m_currentDownload.status = DownloadStatus::Downloading;
    m_currentDownload.startTime = QDateTime::currentDateTime();

    logMessage(QString("Retrying download with video password..."));
    startDownloadProcess(m_currentDownload);
}

void DownloadQueue::startQueue()
{
    if (m_isRunning) {
        return;
    }
    
    m_isRunning = true;
    m_isPaused = false;
    
    logMessage("=== Starting Download Queue ===");
    processNextDownload();
}

void DownloadQueue::pauseQueue()
{
    m_isPaused = true;
    
    if (m_currentProcess && m_currentProcess->state() == QProcess::Running) {
        logMessage("=== Pausing Download Queue ===");
        logMessage("Current download will finish, then queue will pause");
    } else {
        m_isRunning = false;
        logMessage("=== Download Queue Paused ===");
    }
}

void DownloadQueue::clearQueue()
{
    QMutexLocker locker(&m_queueMutex);
    
    // Cancel current download if running
    if (m_currentProcess && m_currentProcess->state() == QProcess::Running) {
        cancelCurrentDownload();
    }
    
    m_queue.clear();
    m_isRunning = false;
    m_isPaused = false;
    
    logMessage("=== Download Queue Cleared ===");
    updateProgressLabel();
    emit queueStatusChanged(m_completedCount, m_totalCount);
}

void DownloadQueue::resetQueue()
{
    QMutexLocker locker(&m_queueMutex);
    
    // Cancel current download if running
    if (m_currentProcess && m_currentProcess->state() == QProcess::Running) {
        cancelCurrentDownload();
    }
    
    // Clear everything and reset counters
    m_queue.clear();
    m_completedDownloads.clear();
    m_completedCount = 0;
    m_totalCount = 0;
    m_isRunning = false;
    m_isPaused = false;
    m_hasCurrentDownload = false;
    
    logMessage("=== Download Queue Reset - All counters cleared ===");
    updateProgressLabel();
    emit queueStatusChanged(0, 0);
}

void DownloadQueue::cancelCurrentDownload()
{
    if (m_currentProcess && m_currentProcess->state() == QProcess::Running) {
        logMessage("=== Cancelling Current Download ===");
        m_currentDownload.status = DownloadStatus::Cancelled;
        killCurrentProcessTree();
        m_currentProcess->waitForFinished(3000);
    }
}

void DownloadQueue::processNextDownload()
{
    QMutexLocker locker(&m_queueMutex);
    
    // Check if paused
    if (m_isPaused) {
        m_isRunning = false;
        logMessage("=== Queue Paused ===");
        return;
    }
    
    // Check if queue is empty
    if (m_queue.isEmpty()) {
        m_isRunning = false;
        m_hasCurrentDownload = false;
        
        if (m_completedCount > 0) {
            logMessage("=== All Downloads Completed ===");
            logMessage(QString("Total downloads processed: %1").arg(m_completedCount));
        }
        
        emit queueFinished();
        return;
    }
    
    // Get next download
    m_currentDownload = m_queue.dequeue();
    m_hasCurrentDownload = true;
    m_currentDownload.status = DownloadStatus::Downloading;
    m_currentDownload.startTime = QDateTime::currentDateTime();
    
    updateProgressLabel();
    emit downloadStarted(m_currentDownload);
    emit queueStatusChanged(m_completedCount + 1, m_totalCount);
    
    // Start download process
    startDownloadProcess(m_currentDownload);
}

bool DownloadQueue::hasActiveProcess() const
{
    return m_currentProcess && m_currentProcess->state() != QProcess::NotRunning;
}

int DownloadQueue::activeDownloadCount() const
{
    return m_queue.size() + (m_hasCurrentDownload ? 1 : 0);
}

void DownloadQueue::stopAllForShutdown()
{
    QMutexLocker locker(&m_queueMutex);
    m_queue.clear();
    m_isPaused = true;
    m_hasCurrentDownload = false;
    if (m_currentProcess) {
        // Sin senales: que no dispare onDownloadFinished ni encadene la proxima descarga.
        m_currentProcess->disconnect(this);
    }
    cleanupCurrentProcess();
    logMessage("=== Downloads stopped to install an update ===");
}

void DownloadQueue::startDownloadProcess(const DownloadItem &item)
{
    // Clean up any existing process
    cleanupCurrentProcess();
    m_stderrBuffer.clear();

    // Punto unico de swap de tools: aca no queda ningun yt-dlp de esta cola corriendo, asi
    // que si el auto-update dejo algo verificado en staging se activa antes de usarlo.
    if (m_toolsManager) {
        m_toolsManager->applyStagedTools();
    }

    // Create new process
    m_currentProcess = new QProcess(this);
#ifdef Q_OS_UNIX
    // Grupo de procesos propio: asi killCurrentProcessTree() alcanza a los hijos de yt-dlp.
    m_currentProcess->setChildProcessModifier([]() { ::setpgid(0, 0); });
#endif
    
    // Connect signals
    connect(m_currentProcess, &QProcess::readyReadStandardOutput, this, &DownloadQueue::onDownloadOutput);
    connect(m_currentProcess, &QProcess::readyReadStandardError, this, &DownloadQueue::onDownloadError);
    connect(m_currentProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &DownloadQueue::onDownloadFinished);
    
    // Prepare yt-dlp arguments
    QStringList arguments;
    
    // Autenticacion: cookies de una sesion ya iniciada en el navegador, o un cookies.txt.
    // Ya no se usa -u/-p: pedirle a cada usuario el mail y la contrasena de su cuenta es
    // inseguro, y ademas YouTube no acepta login por contrasena desde yt-dlp.
    if (!item.cookiesFile.isEmpty()) {
        arguments << "--cookies" << item.cookiesFile;
    } else if (!item.cookiesBrowser.isEmpty()) {
        arguments << "--cookies-from-browser" << item.cookiesBrowser;
    }

    // Add video password if provided
    if (!item.videoPassword.isEmpty()) {
        arguments << "--video-password" << item.videoPassword;
    }
    
    // Use a safer output template that avoids problematic characters
    arguments << "--output" << item.downloadDir + "/%(title).200s.%(ext)s";
    arguments << "--restrict-filenames"; // Restrict filenames to ASCII characters

    // For Vimeo videos, don't specify restrictive format - let yt-dlp choose best available
    // This handles cases where only HLS streaming formats are available
    if (!item.url.contains("vimeo.com", Qt::CaseInsensitive)) {
        // For non-Vimeo sites (like YouTube), use MP4-preferred formats
        arguments << "--format" << "bestvideo[ext=mp4]+bestaudio[ext=m4a]/best[ext=mp4]/best";
    }
    // For Vimeo, omit --format entirely to let yt-dlp choose the best available format
    
    // Add ffmpeg location for proper merging
    QString ffmpegPath = m_toolsManager->getFfmpegPath();
    if (!ffmpegPath.isEmpty() && ffmpegPath != "ffmpeg") {
        arguments << "--ffmpeg-location" << ffmpegPath;
        logMessage(QString("Using ffmpeg location: %1").arg(ffmpegPath));
    } else {
        logMessage("WARNING: ffmpeg path not found or using system ffmpeg");
    }
    
    // YouTube exige un runtime de JavaScript para resolver sus desafios (firma y "n").
    // yt-dlp solo busca deno en el PATH por defecto, asi que se le pasa la ruta explicita
    // del deno que distribuye la app. Antes esto solo se hacia en macOS: en Windows nunca
    // se pasaba y yt-dlp avisaba "No supported JavaScript runtime could be found".
    if (item.url.contains("youtube.com", Qt::CaseInsensitive) || item.url.contains("youtu.be", Qt::CaseInsensitive)) {
        if (m_toolsManager && m_toolsManager->isDenoInstalled()) {
            QString denoPath = m_toolsManager->getDenoPath();
            if (!denoPath.isEmpty()) {
                arguments << "--js-runtimes" << QString("deno:%1").arg(denoPath);
            }
        } else {
            logMessage("WARNING: Deno runtime not found; YouTube downloads may fail or miss formats. It installs automatically at startup; if that failed, use 'Retry tools install' in Settings.");
        }
    }
    
    arguments << item.url;
    
    // Activate progress bar and show percentage text
    m_progressBar->setTextVisible(true);
    m_progressBar->setValue(0);
    
    // Reset fragment tracking for new download
    m_totalFragments = 0;
    m_currentFragment = 0;
    
    // Log start
    logMessage(QString("=== Starting Download %1 of %2 ===").arg(m_completedCount + 1).arg(m_totalCount));
    logMessage(QString("URL: %1").arg(item.url));
    if (!item.cookiesFile.isEmpty()) {
        logMessage(QString("Login: cookies file %1").arg(item.cookiesFile));
    } else if (!item.cookiesBrowser.isEmpty()) {
        logMessage(QString("Login: cookies from %1").arg(item.cookiesBrowser));
    } else {
        logMessage("Login: none (public videos only)");
    }
    logMessage(QString("Download Folder: %1").arg(item.downloadDir));
    logMessage("---");

    // Start process
    QString ytDlpPath = m_toolsManager->getYtDlpPath();
    QString commandLog = arguments.join(" ");
    // La contrasena de VIDEO (no de la cuenta) no debe quedar en el log.
    if (!item.videoPassword.isEmpty()) {
        commandLog = commandLog.replace(item.videoPassword, "***");
    }
    logMessage(QString("Executing: %1 %2").arg(ytDlpPath).arg(commandLog));
    m_currentProcess->start(ytDlpPath, arguments);
    
    if (!m_currentProcess->waitForStarted(5000)) {
        logMessage("ERROR: Could not start yt-dlp. Verify it's installed.");
        m_currentDownload.status = DownloadStatus::Failed;
        m_currentDownload.errorMessage = "Could not start yt-dlp";
        onDownloadFinished(-1, QProcess::CrashExit);
    }
}

void DownloadQueue::onDownloadOutput()
{
    if (!m_currentProcess) return;
    
    QByteArray data = m_currentProcess->readAllStandardOutput();
    QString output = QString::fromUtf8(data).trimmed();
    
    if (!output.isEmpty()) {
        logMessage(output);
        
        // Check for total fragments info (YouTube HLS downloads)
        QRegularExpression fragmentsRegex("\\[hlsnative\\] Total fragments: (\\d+)");
        QRegularExpressionMatch fragmentsMatch = fragmentsRegex.match(output);
        if (fragmentsMatch.hasMatch()) {
            m_totalFragments = fragmentsMatch.captured(1).toInt();
            logMessage(QString("Detected HLS download with %1 fragments").arg(m_totalFragments));
        }
        
        // Parse progress from yt-dlp output
        QRegularExpression progressRegex("\\[download\\]\\s+(\\d+(?:\\.\\d+)?)%.*\\(frag (\\d+)/(\\d+)\\)");
        QRegularExpressionMatch match = progressRegex.match(output);
        
        if (match.hasMatch()) {
            // Fragment-based progress (YouTube HLS)
            bool ok;
            double fragmentProgress = match.captured(1).toDouble(&ok);
            int currentFrag = match.captured(2).toInt();
            int totalFrag = match.captured(3).toInt();
            
            if (ok && totalFrag > 0) {
                // Update fragment info if we have it
                if (m_totalFragments == 0) {
                    m_totalFragments = totalFrag;
                }
                m_currentFragment = currentFrag;
                
                // Calculate overall progress: (completed fragments + current fragment progress) / total fragments
                double overallProgress = ((double)(currentFrag - 1) + (fragmentProgress / 100.0)) / (double)totalFrag * 100.0;
                int progressInt = static_cast<int>(overallProgress);
                
                // Ensure progress doesn't exceed 100% and is monotonic
                progressInt = qMin(progressInt, 100);
                if (progressInt >= m_currentDownload.progress) {
                    m_currentDownload.progress = progressInt;
                    m_progressBar->setValue(progressInt);
                    emit downloadProgress(progressInt);
                }
            }
        } else {
            // Regular progress (Vimeo or non-fragmented downloads)
            QRegularExpression simpleProgressRegex("\\[download\\]\\s+(\\d+(?:\\.\\d+)?)%");
            QRegularExpressionMatch simpleMatch = simpleProgressRegex.match(output);
            if (simpleMatch.hasMatch()) {
                bool ok;
                double progress = simpleMatch.captured(1).toDouble(&ok);
                if (ok) {
                    int progressInt = static_cast<int>(progress);
                    m_currentDownload.progress = progressInt;
                    m_progressBar->setValue(progressInt);
                    emit downloadProgress(progressInt);
                }
            }
        }
        
        // Check for completion
        if (output.contains("100% of") && output.contains("in ")) {
            m_progressBar->setValue(100);
            m_currentDownload.progress = 100;
        }
        
        // Extract title if available
        if (m_currentDownload.title.isEmpty()) {
            QRegularExpression titleRegex("\\[download\\] Destination: (.+)");
            QRegularExpressionMatch titleMatch = titleRegex.match(output);
            if (titleMatch.hasMatch()) {
                QString fullPath = titleMatch.captured(1);
                QStringList pathParts = fullPath.split("/");
                if (!pathParts.isEmpty()) {
                    m_currentDownload.title = pathParts.last();
                }
            }
        }
    }
}

void DownloadQueue::onDownloadError()
{
    if (!m_currentProcess) return;
    
    // stderr de yt-dlp NO es solo errores: ahi van tambien los WARNING y los [debug], y cada
    // linea ya trae su propio prefijo. Antes se le anteponia "ERROR: " a todo el bloque, y un
    // WARNING inofensivo se leia en el log como "ERROR: WARNING: ...". Se loguea linea por
    // linea tal cual; el fragmento sin salto de linea final se guarda hasta el proximo chunk.
    m_stderrBuffer += QString::fromUtf8(m_currentProcess->readAllStandardError());
    int newlineIndex;
    while ((newlineIndex = m_stderrBuffer.indexOf('\n')) >= 0) {
        QString line = m_stderrBuffer.left(newlineIndex).trimmed();
        m_stderrBuffer.remove(0, newlineIndex + 1);
        if (!line.isEmpty()) {
            logMessage(line);
            m_currentDownload.errorMessage += line + "\n";
        }
    }
}

void DownloadQueue::flushStderrBuffer()
{
    QString rest = m_stderrBuffer.trimmed();
    m_stderrBuffer.clear();
    if (!rest.isEmpty()) {
        logMessage(rest);
        m_currentDownload.errorMessage += rest + "\n";
    }
}

void DownloadQueue::logFailureHint(const DownloadItem &item)
{
    // Traduce los errores conocidos de yt-dlp a una indicacion accionable para el usuario.
    // Los textos se comparan contra la salida real de yt-dlp 2026.08 en Windows.
    const QString &err = item.errorMessage;
    const QString browser = item.cookiesBrowser.isEmpty() ? QStringLiteral("the browser") : item.cookiesBrowser;
    QString hint;

    if (err.contains("Could not copy Chrome cookie database", Qt::CaseInsensitive)) {
        // Chromium bloquea su base de cookies mientras el navegador (o su proceso en segundo plano) esta abierto.
        hint = QString("Could not read the cookies because %1 is open. Close %1 completely "
                       "(also from the system tray) and retry, or switch to Firefox or a cookies.txt file.").arg(browser);
    } else if (err.contains("Failed to decrypt with DPAPI", Qt::CaseInsensitive)
               || err.contains("app-bound", Qt::CaseInsensitive)) {
        // Chrome/Edge/Brave en Windows cifran las cookies con "app-bound encryption", que yt-dlp no puede descifrar.
        hint = QString("%1 encrypts its cookies on Windows and they cannot be read. "
                       "Use Firefox (sign in there) or export a cookies.txt file.").arg(browser);
    } else if (err.contains("could not find", Qt::CaseInsensitive) && err.contains("cookies database", Qt::CaseInsensitive)) {
        hint = QString("No cookies found for %1. Is it installed and has it been opened at least once?").arg(browser);
    } else if (err.contains("Sign in to confirm your age", Qt::CaseInsensitive)
               || err.contains("Sign in to confirm you", Qt::CaseInsensitive)
               || err.contains("only works when logged-in", Qt::CaseInsensitive)
               || err.contains("--cookies-from-browser or --cookies", Qt::CaseInsensitive)) {
        if (item.cookiesBrowser.isEmpty() && item.cookiesFile.isEmpty()) {
            hint = "This video requires a signed-in account. In Settings, choose a browser where you are "
                   "signed in (Firefox works best on Windows) or a cookies.txt file.";
        } else {
            hint = "This video requires a signed-in account, but the selected cookies have no valid session. "
                   "Sign in to the site in that browser (or export a fresh cookies.txt) and retry.";
        }
    } else if (err.contains("Requested format is not available", Qt::CaseInsensitive)) {
        hint = "The requested format is not available. yt-dlp and Deno update at startup: restart the app and retry.";
    }

    if (!hint.isEmpty()) {
        logMessage("HINT: " + hint);
    }
}

void DownloadQueue::onDownloadFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_hasCurrentDownload) return;

    // Vaciar lo que quede de stderr antes de evaluar el resultado
    onDownloadError();
    flushStderrBuffer();

    // Deactivate progress bar and hide percentage text
    m_progressBar->setTextVisible(false);
    m_progressBar->setValue(0);
    m_currentDownload.finishTime = QDateTime::currentDateTime();

    if (exitStatus == QProcess::CrashExit) {
        m_currentDownload.status = DownloadStatus::Failed;
        if (m_currentDownload.errorMessage.isEmpty()) {
            m_currentDownload.errorMessage = "Process crashed unexpectedly";
        }
        logMessage("ERROR: yt-dlp process crashed unexpectedly");
        emit downloadFailed(m_currentDownload, m_currentDownload.errorMessage);
    } else if (exitCode == 0) {
        m_currentDownload.status = DownloadStatus::Completed;
        m_currentDownload.progress = 100;
        logMessage("=== Download completed successfully ===");
        emit downloadCompleted(m_currentDownload);
    } else {
        // Check if the error is about video password protection
        bool isVideoPasswordError = m_currentDownload.errorMessage.contains("This video is protected by a password", Qt::CaseInsensitive) ||
                                   m_currentDownload.errorMessage.contains("--video-password", Qt::CaseInsensitive);

        if (isVideoPasswordError && m_currentDownload.videoPassword.isEmpty()) {
            // Show video password dialog and retry
            logMessage("Video password required. Showing password dialog...");
            emit videoPasswordRequired(m_currentDownload);
            return; // Don't mark as failed yet, will retry after password is entered
        } else {
            m_currentDownload.status = DownloadStatus::Failed;
            if (m_currentDownload.errorMessage.isEmpty()) {
                m_currentDownload.errorMessage = QString("Process finished with error code: %1").arg(exitCode);
            }
            logMessage(QString("ERROR: yt-dlp finished with error code: %1").arg(exitCode));
            logFailureHint(m_currentDownload);
            emit downloadFailed(m_currentDownload, m_currentDownload.errorMessage);
        }
    }

    // Add to completed downloads
    m_completedDownloads.append(m_currentDownload);
    m_completedCount++;
    m_hasCurrentDownload = false;

    updateProgressLabel();
    emit queueStatusChanged(m_completedCount, m_totalCount);

    // Clean up process
    cleanupCurrentProcess();

    // Process next download after a short delay
    QTimer::singleShot(1000, this, &DownloadQueue::processNextDownload);
}

DownloadItem DownloadQueue::getCurrentDownload() const
{
    return m_hasCurrentDownload ? m_currentDownload : DownloadItem();
}

void DownloadQueue::updateProgressLabel()
{
    if (m_progressGroup) {
        int currentNumber = m_hasCurrentDownload ? m_completedCount + 1 : m_completedCount;
        QString text = QString("Progress (%1/%2)").arg(currentNumber).arg(m_totalCount);
        m_progressGroup->setTitle(text);
    }
}

void DownloadQueue::logMessage(const QString &message)
{
    if (m_logOutput) {
        m_logOutput->append(message);
    }
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
        if (m_currentProcess->state() == QProcess::Running) {
            killCurrentProcessTree();
            m_currentProcess->waitForFinished(3000);
        }
        m_currentProcess->deleteLater();
        m_currentProcess = nullptr;
    }
}

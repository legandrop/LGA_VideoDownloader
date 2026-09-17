#include "videodownloader/toolsmanager.h"
#include "videodownloader/toolsupdater.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QStyle>
#include <QSysInfo>
#include <QTimer>

namespace {

// Copia que viene con la instalacion (Windows `<app>/tools`) o adentro del bundle (macOS
// `Contents/MacOS/toolsmac`). Es el "seed": se usa mientras la carpeta de usuario no tenga
// su propia copia actualizada. Vacio si no existe.
QString seedToolPath(ToolsUpdater::Tool tool)
{
#ifdef Q_OS_WIN
    const QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/tools/") + ToolsUpdater::binaryName(tool);
#else
    const QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/toolsmac/") + ToolsUpdater::binaryName(tool);
#endif
    return QFileInfo(path).isFile() ? path : QString();
}

// Orden de resolucion de yt-dlp/deno: carpeta de usuario -> seed. Vacio si no hay ninguno
// (en macOS el llamador cae despues a Homebrew/PATH).
QString localToolPath(ToolsUpdater::Tool tool)
{
    const QString user = ToolsUpdater::installedBinary(tool);
    return user.isEmpty() ? seedToolPath(tool) : user;
}

} // namespace

ToolsManager::ToolsManager(QObject *parent)
    : QObject(parent)
    , m_ytDlpInstalled(false)
    , m_ffmpegInstalled(false)
    , m_denoInstalled(false)
    , m_checkingTools(false)
    , m_networkManager(nullptr)
    , m_pendingProcesses(0)
    , m_toolsUpdater(nullptr)
    , m_autoUpdateAttempted(false)
{
    // Initialize network manager
    m_networkManager = new QNetworkAccessManager(this);

    m_toolsUpdater = new ToolsUpdater(this);
    connect(m_toolsUpdater, &ToolsUpdater::logMessage, this, &ToolsManager::logMessage);
    connect(m_toolsUpdater, &ToolsUpdater::runningChanged, this, [this](bool running) {
        emit toolsUpdateRunningChanged(running);
        if (running && !m_checkingTools) {
            updateButtonState();
        }
    });
    connect(m_toolsUpdater, &ToolsUpdater::toolStaged, this, [this]() {
        // Si la cola esta quieta se activa ya; si hay un yt-dlp corriendo, espera al
        // proximo lanzamiento de proceso (DownloadQueue) o al proximo arranque.
        if (!m_processActiveProbe || !m_processActiveProbe()) {
            applyStagedTools();
        } else {
            logMessage("Tools update is ready and will be used by the next download");
        }
    });
    connect(m_toolsUpdater, &ToolsUpdater::finished, this, [this](bool) {
        checkToolsInstallation();
        refreshToolVersions();
    });

    // Arranque: no hay procesos todavia, asi que es un momento valido para el swap.
    ToolsUpdater::cleanupLeftovers();
    applyStagedTools();
}

void ToolsManager::startAutomaticUpdate()
{
    m_autoUpdateAttempted = true;
    m_toolsUpdater->start();
}

bool ToolsManager::isUpdatingTools() const
{
    return m_toolsUpdater && m_toolsUpdater->isRunning();
}

bool ToolsManager::applyStagedTools()
{
    QStringList lines;
    const QStringList swapped = ToolsUpdater::applyStaged(&lines);
    for (const QString &line : lines) {
        logMessage(line);
    }
    if (swapped.isEmpty()) {
        return false;
    }
    if (swapped.contains(QStringLiteral("yt-dlp"))) {
        m_ytDlpInstalled = true;
    }
    if (swapped.contains(QStringLiteral("deno"))) {
        m_denoInstalled = true;
    }
    if (!m_checkingTools) {
        updateButtonState();
    }
    return true;
}

void ToolsManager::refreshToolVersions()
{
    struct Probe { QString key; QString program; QString arg; };
    const QList<Probe> probes = {
        {QStringLiteral("yt-dlp"), getYtDlpPath(), QStringLiteral("--version")},
        {QStringLiteral("deno"), getDenoPath(), QStringLiteral("--version")},
        {QStringLiteral("ffmpeg"), getFfmpegPath(), QStringLiteral("-version")},
    };
    for (const Probe &probe : probes) {
        QProcess *process = new QProcess(this);
        const QString key = probe.key;
        connect(process, &QProcess::finished, this, [this, process, key](int exitCode, QProcess::ExitStatus status) {
            process->deleteLater();
            QString version;
            if (status == QProcess::NormalExit && exitCode == 0) {
                const QString first = QString::fromUtf8(process->readAllStandardOutput()).trimmed().section(QLatin1Char('\n'), 0, 0).trimmed();
                // "2026.08.19" / "deno 2.9.6 (stable, ...)" / "ffmpeg version 7.1-full_build ..."
                if (key == QLatin1String("deno")) {
                    version = first.section(QLatin1Char(' '), 1, 1);
                } else if (key == QLatin1String("ffmpeg")) {
                    version = first.section(QLatin1Char(' '), 2, 2);
                } else {
                    version = first;
                }
            }
            m_toolVersions.insert(key, version);
            emit toolVersionsChanged();
        });
        connect(process, &QProcess::errorOccurred, this, [this, process, key](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                process->deleteLater();
                m_toolVersions.insert(key, QString());
                emit toolVersionsChanged();
            }
        });
        process->start(probe.program, {probe.arg});
    }
}

ToolsManager::~ToolsManager()
{
    // Qt handles cleanup automatically
}

void ToolsManager::checkToolsInstallation()
{
    if (m_checkingTools) {
        return; // Already checking
    }
    
    m_checkingTools = true;
    m_pendingProcesses = 0;
    
    logMessage("Checking tools installation...");
    
    // Check both tools
    checkDenoInstallation();
    checkYtDlpInstallation();
    checkFfmpegInstallation();
}

void ToolsManager::checkYtDlpInstallation()
{
#ifdef Q_OS_WIN
    // Windows: carpeta de usuario (auto-update) y, si no hay, la copia de la instalacion
    QString ytDlpPath = localToolPath(ToolsUpdater::Tool::YtDlp);

    if (!ytDlpPath.isEmpty()) {
        m_ytDlpInstalled = true;
        logMessage(QString("✓ yt-dlp.exe found: %1").arg(QDir::toNativeSeparators(ytDlpPath)));
    } else {
        m_ytDlpInstalled = false;
        logMessage("✗ yt-dlp.exe not found");
    }
    
    // Check ffmpeg after yt-dlp check is done
    if (m_pendingProcesses == 0) {
        QTimer::singleShot(100, this, &ToolsManager::updateButtonState);
    }
    return;
#endif
    
#ifdef Q_OS_MAC
    // macOS: carpeta de usuario, despues toolsmac del bundle, despues Homebrew/PATH
    QString ytDlpPath = localToolPath(ToolsUpdater::Tool::YtDlp);

    if (!ytDlpPath.isEmpty()) {
        m_ytDlpInstalled = true;
        logMessage(QString("✓ yt-dlp found: %1").arg(ytDlpPath));
        
        // Check ffmpeg after yt-dlp check is done
        if (m_pendingProcesses == 0) {
            QTimer::singleShot(100, this, &ToolsManager::updateButtonState);
        }
        return;
    }
    
    // Fallback: Check in common Homebrew locations first, then PATH
    QStringList possiblePaths = {
        "/opt/homebrew/bin/yt-dlp",  // Apple Silicon Homebrew
        "/usr/local/bin/yt-dlp",    // Intel Homebrew
        "yt-dlp"                    // System PATH (fallback)
    };
    
    QString foundPath;
    for (const QString &path : possiblePaths) {
        if (path == "yt-dlp") {
            // Try PATH version
            break;
        } else if (QFile::exists(path)) {
            foundPath = path;
            break;
        }
    }
    
    if (!foundPath.isEmpty()) {
        // Found in Homebrew location, verify it works
        m_pendingProcesses++;
        QProcess *process = new QProcess(this);
        
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [this, process, foundPath](int exitCode, QProcess::ExitStatus exitStatus) {
            
            if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                m_ytDlpInstalled = true;
                logMessage(QString("✓ yt-dlp found at: %1").arg(foundPath));
            } else {
                m_ytDlpInstalled = false;
                logMessage("✗ yt-dlp found but not working properly");
            }
            
            m_pendingProcesses--;
            if (m_pendingProcesses == 0) {
                updateButtonState();
            }
            
            process->deleteLater();
        });
        
        process->start(foundPath, QStringList() << "--version");
        
        if (!process->waitForStarted(3000)) {
            m_ytDlpInstalled = false;
            logMessage("✗ yt-dlp found but failed to start");
            m_pendingProcesses--;
            if (m_pendingProcesses == 0) {
                updateButtonState();
            }
            process->deleteLater();
        }
    } else {
        // Fallback to PATH check
        m_pendingProcesses++;
        QProcess *process = new QProcess(this);
        
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
            
            if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                m_ytDlpInstalled = true;
                logMessage("✓ yt-dlp is installed and available");
            } else {
                m_ytDlpInstalled = false;
                logMessage("✗ yt-dlp is not installed");
            }
            
            m_pendingProcesses--;
            if (m_pendingProcesses == 0) {
                updateButtonState();
            }
            
            process->deleteLater();
        });
        
        process->start("yt-dlp", QStringList() << "--version");
        
        if (!process->waitForStarted(3000)) {
            m_ytDlpInstalled = false;
            logMessage("✗ yt-dlp is not installed");
            m_pendingProcesses--;
            if (m_pendingProcesses == 0) {
                updateButtonState();
            }
            process->deleteLater();
        }
    }
#else
    // Linux: Check via PATH
    m_pendingProcesses++;
    QProcess *process = new QProcess(this);
    
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            m_ytDlpInstalled = true;
            logMessage("✓ yt-dlp is installed and available");
        } else {
            m_ytDlpInstalled = false;
            logMessage("✗ yt-dlp is not installed");
        }
        
        m_pendingProcesses--;
        if (m_pendingProcesses == 0) {
            updateButtonState();
        }
        
        process->deleteLater();
    });
    
    // Check if yt-dlp is available
    process->start("yt-dlp", QStringList() << "--version");
    
    if (!process->waitForStarted(3000)) {
        m_ytDlpInstalled = false;
        logMessage("✗ yt-dlp is not installed");
        m_pendingProcesses--;
        if (m_pendingProcesses == 0) {
            updateButtonState();
        }
        process->deleteLater();
    }
#endif
}

void ToolsManager::checkFfmpegInstallation()
{
#ifdef Q_OS_WIN
    // Windows: Check if ffmpeg.exe exists in the tools subdirectory
    QString appDir = QCoreApplication::applicationDirPath();
    QString ffmpegPath = appDir + "/tools/ffmpeg.exe";
    
    if (QFile::exists(ffmpegPath)) {
        m_ffmpegInstalled = true;
        logMessage("✓ ffmpeg.exe found in tools directory");
    } else {
        m_ffmpegInstalled = false;
        logMessage("✗ ffmpeg.exe not found in tools directory");
    }
    
    // Update button state after both checks are done
    if (m_pendingProcesses == 0) {
        QTimer::singleShot(100, this, &ToolsManager::updateButtonState);
    }
    return;
#endif
    
#ifdef Q_OS_MAC
    // macOS: Check if ffmpeg exists in the toolsmac subdirectory first, then fallback to system
    QString appDir = QCoreApplication::applicationDirPath();
    QString ffmpegPath = appDir + "/toolsmac/ffmpeg";
    
    if (QFile::exists(ffmpegPath)) {
        m_ffmpegInstalled = true;
        logMessage("✓ ffmpeg found in toolsmac directory");
        
        // Update button state after both checks are done
        if (m_pendingProcesses == 0) {
            QTimer::singleShot(100, this, &ToolsManager::updateButtonState);
        }
        return;
    }
    
    // Fallback: Check in common Homebrew locations first, then PATH
    QStringList possiblePaths = {
        "/opt/homebrew/bin/ffmpeg",  // Apple Silicon Homebrew
        "/usr/local/bin/ffmpeg",    // Intel Homebrew
        "ffmpeg"                    // System PATH (fallback)
    };
    
    QString foundPath;
    for (const QString &path : possiblePaths) {
        if (path == "ffmpeg") {
            // Try PATH version
            break;
        } else if (QFile::exists(path)) {
            foundPath = path;
            break;
        }
    }
    
    if (!foundPath.isEmpty()) {
        // Found in Homebrew location, verify it works
        m_pendingProcesses++;
        QProcess *process = new QProcess(this);
        
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [this, process, foundPath](int exitCode, QProcess::ExitStatus exitStatus) {
            
            if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                m_ffmpegInstalled = true;
                logMessage(QString("✓ ffmpeg found at: %1").arg(foundPath));
            } else {
                m_ffmpegInstalled = false;
                logMessage("✗ ffmpeg found but not working properly");
            }
            
            m_pendingProcesses--;
            if (m_pendingProcesses == 0) {
                updateButtonState();
            }
            
            process->deleteLater();
        });
        
        process->start(foundPath, QStringList() << "-version");
        
        if (!process->waitForStarted(3000)) {
            m_ffmpegInstalled = false;
            logMessage("✗ ffmpeg found but failed to start");
            m_pendingProcesses--;
            if (m_pendingProcesses == 0) {
                updateButtonState();
            }
            process->deleteLater();
        }
    } else {
        // Fallback to PATH check
        m_pendingProcesses++;
        QProcess *process = new QProcess(this);
        
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
            
            if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                m_ffmpegInstalled = true;
                logMessage("✓ ffmpeg is installed and available");
            } else {
                m_ffmpegInstalled = false;
                logMessage("✗ ffmpeg is not installed");
            }
            
            m_pendingProcesses--;
            if (m_pendingProcesses == 0) {
                updateButtonState();
            }
            
            process->deleteLater();
        });
        
        process->start("ffmpeg", QStringList() << "-version");
        
        if (!process->waitForStarted(3000)) {
            m_ffmpegInstalled = false;
            logMessage("✗ ffmpeg is not installed");
            m_pendingProcesses--;
            if (m_pendingProcesses == 0) {
                updateButtonState();
            }
            process->deleteLater();
        }
    }
#else
    // Linux: Check via PATH
    m_pendingProcesses++;
    QProcess *process = new QProcess(this);
    
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            m_ffmpegInstalled = true;
            logMessage("✓ ffmpeg is installed and available");
        } else {
            m_ffmpegInstalled = false;
            logMessage("✗ ffmpeg is not installed");
        }
        
        m_pendingProcesses--;
        if (m_pendingProcesses == 0) {
            updateButtonState();
        }
        
        process->deleteLater();
    });
    
    // Check if ffmpeg is available
    process->start("ffmpeg", QStringList() << "-version");
    
    if (!process->waitForStarted(3000)) {
        m_ffmpegInstalled = false;
        logMessage("✗ ffmpeg is not installed");
        m_pendingProcesses--;
        if (m_pendingProcesses == 0) {
            updateButtonState();
        }
        process->deleteLater();
    }
#endif
}

void ToolsManager::checkDenoInstallation()
{
#ifdef Q_OS_WIN
    // Windows: deno.exe en la carpeta de usuario (lo instala el auto-update) o en la
    // instalacion. Sin deno, yt-dlp no resuelve los desafios JS de YouTube.
    QString denoPath = localToolPath(ToolsUpdater::Tool::Deno);
    if (!denoPath.isEmpty()) {
        m_denoInstalled = true;
        logMessage(QString("✓ deno.exe found: %1").arg(QDir::toNativeSeparators(denoPath)));
    } else {
        m_denoInstalled = false;
        logMessage("✗ deno.exe not found (needed for YouTube)");
    }
    return;
#endif

#ifdef Q_OS_MAC
    // macOS: carpeta de usuario, despues toolsmac del bundle, despues Homebrew/PATH
    QString denoPath = localToolPath(ToolsUpdater::Tool::Deno);

    if (!denoPath.isEmpty()) {
        m_denoInstalled = true;
        logMessage(QString("✓ deno found: %1").arg(denoPath));
        
        if (m_pendingProcesses == 0) {
            QTimer::singleShot(100, this, &ToolsManager::updateButtonState);
        }
        return;
    }
    
    // Fallback: Check in common Homebrew locations first, then PATH
    QStringList possiblePaths = {
        "/opt/homebrew/bin/deno",  // Apple Silicon Homebrew
        "/usr/local/bin/deno",    // Intel Homebrew
        "deno"                    // System PATH (fallback)
    };
    
    QString foundPath;
    for (const QString &path : possiblePaths) {
        if (path == "deno") {
            // Try PATH version
            break;
        } else if (QFile::exists(path)) {
            foundPath = path;
            break;
        }
    }
    
    if (!foundPath.isEmpty()) {
        // Found in Homebrew location, verify it works
        m_pendingProcesses++;
        QProcess *process = new QProcess(this);
        
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [this, process, foundPath](int exitCode, QProcess::ExitStatus exitStatus) {
            
            if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                m_denoInstalled = true;
                logMessage(QString("✓ deno found at: %1").arg(foundPath));
            } else {
                m_denoInstalled = false;
                logMessage("✗ deno found but not working properly");
            }
            
            m_pendingProcesses--;
            if (m_pendingProcesses == 0) {
                updateButtonState();
            }
            
            process->deleteLater();
        });
        
        process->start(foundPath, QStringList() << "--version");
        
        if (!process->waitForStarted(3000)) {
            m_denoInstalled = false;
            logMessage("✗ deno found but failed to start");
            m_pendingProcesses--;
            if (m_pendingProcesses == 0) {
                updateButtonState();
            }
            process->deleteLater();
        }
        return;
    }
    
    // Fallback to PATH check
    m_pendingProcesses++;
    QProcess *process = new QProcess(this);
    
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            m_denoInstalled = true;
            logMessage("✓ deno is installed and available");
        } else {
            m_denoInstalled = false;
            logMessage("✗ deno is not installed");
        }
        
        m_pendingProcesses--;
        if (m_pendingProcesses == 0) {
            updateButtonState();
        }
        
        process->deleteLater();
    });
    
    process->start("deno", QStringList() << "--version");
    
    if (!process->waitForStarted(3000)) {
        m_denoInstalled = false;
        logMessage("✗ deno is not installed");
        m_pendingProcesses--;
        if (m_pendingProcesses == 0) {
            updateButtonState();
        }
        process->deleteLater();
    }
#else
    m_denoInstalled = false;
#endif
}

void ToolsManager::updateButtonState()
{
    m_checkingTools = false;
    
    bool allInstalled = m_ytDlpInstalled && m_ffmpegInstalled;
#if defined(Q_OS_MAC) || defined(Q_OS_WIN)
    allInstalled = allInstalled && m_denoInstalled;
#endif

    // yt-dlp y deno se instalan y actualizan solos al arrancar: el reintento manual solo
    // se ofrece cuando falta algo y el intento automatico ya fallo.
    if (isUpdatingTools()) {
        setStatus(allInstalled ? Status::Updating : Status::Installing);
    } else if (allInstalled) {
        setStatus(Status::Ready);
    } else if (!m_autoUpdateAttempted) {
        // El auto-update arranca unos segundos despues de abrir la ventana.
        setStatus(Status::Installing);
    } else if (areToolsInstalled()) {
        // Solo falta deno (o quedo en staging hasta la proxima descarga): se puede descargar,
        // y el aviso de YouTube sale en el log de cada descarga. No es "faltan las tools".
        setStatus(Status::Ready);
    } else {
        setStatus(Status::Missing);
    }

    emit toolsStatusChanged(allInstalled);
}

bool ToolsManager::areToolsInstalled() const
{
#ifdef Q_OS_MAC
    return m_ytDlpInstalled && m_ffmpegInstalled && m_denoInstalled;
#else
    return m_ytDlpInstalled && m_ffmpegInstalled;
#endif
}

void ToolsManager::installOrUpdateTools()
{
    retryInstall();
}

void ToolsManager::setStatus(Status status)
{
    if (m_status == status) {
        return;
    }
    m_status = status;
    emit statusChanged(status);
}

void ToolsManager::retryInstall()
{
    setStatus(Status::Installing);

#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    logMessage("=== Installing Tools ===");
    if (!m_ffmpegInstalled) {
#ifdef Q_OS_WIN
        downloadFfmpegWindows();
#else
        downloadFfmpegMac();
#endif
    }
    // yt-dlp y deno: mismo camino verificado que el automatico.
    startAutomaticUpdate();
    return;
#endif
    
#ifdef Q_OS_LINUX
    logMessage("=== Linux Platform ===");
    logMessage("Automatic installation not implemented for Linux yet.");
    logMessage("Please install manually:");
    logMessage("  sudo apt install yt-dlp ffmpeg  # Ubuntu/Debian");
    logMessage("  sudo yum install yt-dlp ffmpeg  # CentOS/RHEL");
    logMessage("  sudo pacman -S yt-dlp ffmpeg    # Arch Linux");
    updateButtonState();
#endif
}

// macOS Download Methods
void ToolsManager::downloadFfmpegMac()
{
#ifdef Q_OS_MAC
    // evermeet.cx URL for latest ffmpeg for macOS
    QString url = "https://evermeet.cx/ffmpeg/getrelease/zip";
    QNetworkRequest request(url);
    
    // Set user agent
    request.setRawHeader("User-Agent", "VideoDownloader/1.0");
    
    logMessage(QString("Downloading ffmpeg from: %1").arg(url));
    
    // Start download
    QNetworkReply *reply = m_networkManager->get(request);
    
    connect(reply, &QNetworkReply::downloadProgress, [this](qint64 received, qint64 total) {
        if (total > 0) {
            int percentage = (received * 100) / total;
            logMessage(QString("ffmpeg download progress: %1% (%2 / %3 bytes)")
                       .arg(percentage)
                       .arg(received)
                       .arg(total));
        }
    });
    
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            // Save the downloaded zip file temporarily
            QString appDir = QCoreApplication::applicationDirPath();
            QString toolsDir = appDir + "/toolsmac";
            
            // Create toolsmac directory if it doesn't exist
            QDir dir;
            if (!dir.exists(toolsDir)) {
                if (!dir.mkpath(toolsDir)) {
                    logMessage("ERROR: Could not create toolsmac directory");
                    updateButtonState();
                    reply->deleteLater();
                    return;
                }
            }
            
            QString tempZipPath = toolsDir + "/ffmpeg_temp.zip";
            QString ffmpegPath = toolsDir + "/ffmpeg";
            
            // Save zip file
            QFile zipFile(tempZipPath);
            if (zipFile.open(QIODevice::WriteOnly)) {
                zipFile.write(reply->readAll());
                zipFile.close();
                
                // Extract ffmpeg binary using system unzip command
                QProcess *unzipProcess = new QProcess(this);
                connect(unzipProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                        [this, unzipProcess, tempZipPath, ffmpegPath](int exitCode, QProcess::ExitStatus exitStatus) {
                    
                    // Clean up zip file
                    QFile::remove(tempZipPath);
                    
                    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                        // Make ffmpeg executable
                        QFile ffmpegFile(ffmpegPath);
                        if (ffmpegFile.exists()) {
                            ffmpegFile.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                                     QFile::ReadGroup | QFile::ExeGroup |
                                                     QFile::ReadOther | QFile::ExeOther);
                            
                            logMessage("=== ffmpeg downloaded and extracted successfully ===");
                            logMessage(QString("Saved to: %1").arg(ffmpegPath));
                            
                            // Check installation after extraction
                            QTimer::singleShot(500, [this]() {
                                checkToolsInstallation();
                            });
                        } else {
                            logMessage("ERROR: ffmpeg binary not found after extraction");
                            updateButtonState();
                        }
                    } else {
                        logMessage("ERROR: Failed to extract ffmpeg zip file");
                        updateButtonState();
                    }
                    
                    unzipProcess->deleteLater();
                });
                
                // Extract only the ffmpeg binary from the zip
                unzipProcess->start("unzip", QStringList() << "-j" << tempZipPath << "ffmpeg" << "-d" << toolsDir);
                
                if (!unzipProcess->waitForStarted(5000)) {
                    logMessage("ERROR: Could not start unzip process");
                    QFile::remove(tempZipPath);
                    updateButtonState();
                    unzipProcess->deleteLater();
                }
            } else {
                logMessage("ERROR: Could not save ffmpeg zip file");
                logMessage("Check write permissions in application directory");
                updateButtonState();
            }
        } else {
            logMessage("ERROR: Failed to download ffmpeg");
            logMessage(QString("Error: %1").arg(reply->errorString()));
            logMessage("Please check your internet connection");
            updateButtonState();
        }
        
        reply->deleteLater();
    });
#endif
}

void ToolsManager::updateFfmpegMac()
{
#ifdef Q_OS_MAC
    // For updates, just download the latest version (same as install)
    downloadFfmpegMac();
#endif
}

// Windows Download Methods
void ToolsManager::downloadFfmpegWindows()
{
#ifdef Q_OS_WIN
    // GitHub URL for latest ffmpeg.exe (using a reliable build)
    QString url = "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/ffmpeg-master-latest-win64-gpl.zip";
    QNetworkRequest request(url);
    
    // Set user agent
    request.setRawHeader("User-Agent", "VideoDownloader/1.0");
    
    logMessage(QString("Downloading ffmpeg from: %1").arg(url));
    logMessage("Note: This will download a zip file that needs to be extracted manually");
    logMessage("For now, please download and extract ffmpeg.exe manually to the application directory");
    
    // TODO: Implement zip extraction
    // For now, just mark as not implemented
    logMessage("ERROR: Automatic ffmpeg installation not fully implemented on Windows yet");
    logMessage("Please download ffmpeg manually from: https://ffmpeg.org/download.html");
    logMessage("Extract ffmpeg.exe to the same directory as this application");
    
    updateButtonState();
#endif
}

// Helper Methods
void ToolsManager::logMessage(const QString &message)
{
    emit logLine(message);
}

QString ToolsManager::getYtDlpPath() const
{
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    // Carpeta de usuario (auto-update) -> copia de la instalacion/bundle
    const QString localPath = localToolPath(ToolsUpdater::Tool::YtDlp);
    if (!localPath.isEmpty()) {
        return localPath;
    }
#endif
#ifdef Q_OS_WIN
    // Sin ninguna copia todavia: donde la va a dejar el auto-update
    return ToolsUpdater::toolsDir() + "/yt-dlp.exe";
#else
    // macOS/Linux: system PATH
    return "yt-dlp";
#endif
}

QString ToolsManager::getFfmpegPath() const
{
#ifdef Q_OS_WIN
    // Windows: Use tools subdirectory
    QString appDir = QCoreApplication::applicationDirPath();
    return appDir + "/tools/ffmpeg.exe";
#elif defined(Q_OS_MAC)
    // macOS: Check toolsmac directory first, then fallback to system PATH
    QString appDir = QCoreApplication::applicationDirPath();
    QString localPath = appDir + "/toolsmac/ffmpeg";
    if (QFile::exists(localPath)) {
        return localPath;
    }
    // Fallback to system PATH
    return "ffmpeg";
#else
    // Linux: Use system PATH
    return "ffmpeg";
#endif
}

QString ToolsManager::getDenoPath() const
{
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    // Carpeta de usuario (auto-update) -> copia de la instalacion/bundle
    const QString localPath = localToolPath(ToolsUpdater::Tool::Deno);
    if (!localPath.isEmpty()) {
        return localPath;
    }
#endif
#ifdef Q_OS_WIN
    return ToolsUpdater::toolsDir() + "/deno.exe";
#else
    // macOS/Linux: system PATH
    return "deno";
#endif
}

QString ToolsManager::getBrewPath() const
{
#ifdef Q_OS_MAC
    // Check for Homebrew in common locations
    QStringList possiblePaths = {
        "/opt/homebrew/bin/brew",  // Apple Silicon Homebrew
        "/usr/local/bin/brew"      // Intel Homebrew
    };
    
    for (const QString &path : possiblePaths) {
        if (QFile::exists(path)) {
            return path;
        }
    }
    
    // Fallback to PATH
    return "brew";
#else
    return "brew";
#endif
}


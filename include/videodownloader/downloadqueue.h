#ifndef DOWNLOADQUEUE_H
#define DOWNLOADQUEUE_H

#include <QObject>
#include <QQueue>
#include <QProcess>
#include <QTextEdit>
#include <QProgressBar>
#include <QGroupBox>
#include <QTimer>
#include <QMutex>

#include "downloaditem.h"

class ToolsManager;

class DownloadQueue : public QObject
{
    Q_OBJECT

public:
    explicit DownloadQueue(QTextEdit *logOutput, QProgressBar *progressBar, QGroupBox *progressGroup, ToolsManager *toolsManager, QObject *parent = nullptr);
    ~DownloadQueue();

    // Queue management
    void addDownload(const QString &url, const QString &cookiesBrowser, const QString &cookiesFile, const QString &downloadDir);
    void retryDownloadWithVideoPassword(const QString &videoPassword);
    void startQueue();
    void pauseQueue();
    void clearQueue();
    void resetQueue(); // Complete reset including counters
    void cancelCurrentDownload();
    
    // Status getters
    bool isRunning() const { return m_isRunning; }
    bool isPaused() const { return m_isPaused; }
    int getCurrentIndex() const { return m_completedCount; }
    int getTotalCount() const { return m_totalCount; }
    int getQueueSize() const { return m_queue.size(); }
    // Hay un proceso de yt-dlp vivo (el swap de tools tiene que esperar).
    bool hasActiveProcess() const;
    // Descargas que se perderian al cerrar: la actual mas las encoladas.
    int activeDownloadCount() const;
    // Para el update de la app: vacia la cola y mata yt-dlp CON sus hijos (ffmpeg, deno,
    // y el proceso real de yt-dlp, que en Windows es hijo del lanzador onefile).
    void stopAllForShutdown();

    // Current download info
    DownloadItem getCurrentDownload() const;
    QList<DownloadItem> getCompletedDownloads() const { return m_completedDownloads; }

signals:
    void downloadStarted(const DownloadItem &item);
    void downloadProgress(int percentage);
    void downloadCompleted(const DownloadItem &item);
    void downloadFailed(const DownloadItem &item, const QString &error);
    void queueFinished();
    void queueStatusChanged(int current, int total);
    void downloadAddedToQueue(int totalCount);
    void videoPasswordRequired(const DownloadItem &item);

private slots:
    void processNextDownload();
    void onDownloadFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onDownloadOutput();
    void onDownloadError();

private:
    void updateProgressLabel();
    void logMessage(const QString &message);
    void startDownloadProcess(const DownloadItem &item);
    void cleanupCurrentProcess();
    // Mata yt-dlp CON sus hijos: el ejecutable onefile relanza el yt-dlp real como hijo, y
    // este a su vez lanza ffmpeg y deno. Matar solo el padre dejaba la descarga huerfana.
    void killCurrentProcessTree();
    void logFailureHint(const DownloadItem &item);
    void flushStderrBuffer();

    // UI references
    QTextEdit *m_logOutput;
    QProgressBar *m_progressBar;
    QGroupBox *m_progressGroup;
    ToolsManager *m_toolsManager;
    
    // Queue management
    QQueue<DownloadItem> m_queue;
    QList<DownloadItem> m_completedDownloads;
    DownloadItem m_currentDownload;
    
    // Process management
    QProcess *m_currentProcess;
    QString m_stderrBuffer; // Linea incompleta de stderr pendiente del proximo chunk
    QMutex m_queueMutex;
    
    // Status tracking
    bool m_isRunning;
    bool m_isPaused;
    int m_completedCount;
    int m_totalCount;
    bool m_hasCurrentDownload;
    
    // Fragment-based progress tracking for YouTube downloads
    int m_totalFragments;
    int m_currentFragment;
};

#endif // DOWNLOADQUEUE_H

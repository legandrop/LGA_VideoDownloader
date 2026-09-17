#ifndef TOOLSMANAGER_H
#define TOOLSMANAGER_H

#include <QObject>
#include <QTextEdit>
#include <QPushButton>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QMap>

#include <functional>

class ToolsUpdater;

class ToolsManager : public QObject
{
    Q_OBJECT

public:
    explicit ToolsManager(QTextEdit *logOutput, QPushButton *toolsButton, QObject *parent = nullptr);
    ~ToolsManager();

    // Public interface
    void checkToolsInstallation();
    void installOrUpdateTools();
    bool areToolsInstalled() const;
    
    // Tool status getters
    bool isYtDlpInstalled() const { return m_ytDlpInstalled; }
    bool isFfmpegInstalled() const { return m_ffmpegInstalled; }
    bool isDenoInstalled() const { return m_denoInstalled; }
    
    // Tool path getters
    QString getYtDlpPath() const;
    QString getFfmpegPath() const;
    QString getDenoPath() const;

    // Auto-update silencioso de yt-dlp y deno en la carpeta de datos del usuario.
    ToolsUpdater *toolsUpdater() const { return m_toolsUpdater; }
    void startAutomaticUpdate();
    bool isUpdatingTools() const;
    // Punto UNICO de swap de lo verificado en staging. Solo sin procesos de yt-dlp vivos:
    // lo llaman el arranque, DownloadQueue antes de lanzar cada proceso y el fin del
    // auto-update si la cola esta quieta. Devuelve true si reemplazo algo.
    bool applyStagedTools();
    // Como saber si hay un yt-dlp corriendo (lo cablea MainWindow contra DownloadQueue).
    void setProcessActiveProbe(std::function<bool()> probe) { m_processActiveProbe = std::move(probe); }

    // Versiones reales de las tools ("yt-dlp", "deno", "ffmpeg"), leidas con --version en
    // segundo plano. Vacio = no instalada o todavia no leida.
    void refreshToolVersions();
    QMap<QString, QString> toolVersions() const { return m_toolVersions; }

signals:
    void toolsStatusChanged(bool allInstalled);
    void installationFinished(bool success);
    void toolsUpdateRunningChanged(bool running);
    void toolVersionsChanged();

private slots:
    void onInstallUpdateClicked();

private:
    // Detection methods
    void checkYtDlpInstallation();
    void checkFfmpegInstallation();
    void checkDenoInstallation();
    void updateButtonState();

    // Installation methods - macOS (ffmpeg; yt-dlp y deno los maneja ToolsUpdater)
    void downloadFfmpegMac();
    void updateFfmpegMac();

    // Installation methods - Windows
    void downloadFfmpegWindows();

    // Helper methods
    void logMessage(const QString &message);
    void setButtonEnabled(bool enabled);
    void setButtonText(const QString &text);
    void setButtonStyle(const QString &styleClass);
    QString getBrewPath() const;
    
    // UI references
    QTextEdit *m_logOutput;
    QPushButton *m_toolsButton;
    
    // Tool status
    bool m_ytDlpInstalled;
    bool m_ffmpegInstalled;
    bool m_denoInstalled;
    bool m_checkingTools;
    
    // Network manager for downloads
    QNetworkAccessManager *m_networkManager;
    
    // Process counters for async operations
    int m_pendingProcesses;

    ToolsUpdater *m_toolsUpdater;
    bool m_autoUpdateAttempted;
    std::function<bool()> m_processActiveProbe;
    QMap<QString, QString> m_toolVersions;
};

#endif // TOOLSMANAGER_H

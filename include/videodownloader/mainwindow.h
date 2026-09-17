#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include <QGroupBox>
#include <QSettings>
#include <QComboBox>

class ToolsManager;
class DownloadQueue;
class UpdateService;
struct DownloadItem;

QT_BEGIN_NAMESPACE
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // Servicios para la UI (ventana principal y dialogo de Help):
    //  - UpdateService: update de la app (estado, version disponible, installAppUpdate()).
    //  - ToolsManager: rutas y versiones de yt-dlp/deno/ffmpeg, auto-update de tools.
    //  - DownloadQueue: activeDownloadCount() para confirmar antes de instalar un update.
    UpdateService *updateService() const { return m_updateService; }
    ToolsManager *toolsManager() const { return m_toolsManager; }
    DownloadQueue *downloadQueue() const { return m_downloadQueue; }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onDownloadClicked();
    void onUrlChanged();
    void onCookiesSourceChanged(int index);
    void onBrowseFolderClicked();
    void onToolsStatusChanged(bool allInstalled);
    void onToolsStatusChangedForInitialState(bool allInstalled);
    void onDownloadStarted();
    void onDownloadCompleted();
    void onQueueStatusChanged(int current, int total);
    void onDownloadAddedToQueue(int totalCount);
    void onCancelClicked();
    void onLogToggleClicked();
    void onSettingsToggleClicked();
    void onVideoPasswordRequired(const DownloadItem &item);

private:
    void setupUI();
    void setupStyles();
    void setupConnections();
    void loadSettings();
    void saveSettings();
    void detectOperatingSystem();
    void adjustWindowSize();
    QString getConfigPath() const;
    bool isValidVideoUrl(const QString &url) const;
    bool isVimeoUrl(const QString &url) const;
    bool isValidDownloadPath(const QString &path) const;
    bool shouldShowSettingsExpanded();
    void setInitialSettingsState();
    // provisional: lo reemplaza el rediseño
    void refreshUpdateLink();
    void onUpdateLinkClicked();

    // UI Components
    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;
    
    QGroupBox *m_inputGroup;
    QVBoxLayout *m_inputLayout;
    QHBoxLayout *m_urlLayout;
    QLineEdit *m_urlInput;
    QPushButton *m_downloadButton;
    
    QGroupBox *m_progressGroup;
    QVBoxLayout *m_progressLayout;
    QHBoxLayout *m_progressButtonLayout;
    QProgressBar *m_progressBar;
    QLabel *m_progressLabel;
    QPushButton *m_cancelButton;
    
    QGroupBox *m_logGroup;
    QVBoxLayout *m_logLayout;
    QTextEdit *m_logOutput;
    bool m_logExpanded;

    QGroupBox *m_settingsGroup;
    QVBoxLayout *m_settingsLayout;
    bool m_settingsExpanded;
    QHBoxLayout *m_credentialsLayout;
    QHBoxLayout *m_folderLayout;
    QHBoxLayout *m_toolsLayout;
    
    // Login row: origen de las cookies (navegador o cookies.txt)
    QLabel *m_cookiesLabel;
    QComboBox *m_cookiesSourceCombo;
    
    // Folder row
    QLineEdit *m_downloadFolderInput;
    QPushButton *m_browseFolderButton;
    QPushButton *m_toolsButton;
    
    // Settings
    QSettings *m_settings;
    
    // Tools manager
    ToolsManager *m_toolsManager;
    
    // Download queue
    DownloadQueue *m_downloadQueue;

    // Update de la app
    UpdateService *m_updateService;
    // provisional: lo reemplaza el rediseño
    QPushButton *m_updateLinkButton;

    // Track maximum width for consistent sizing when settings is collapsed
    int m_maxWindowWidth;
};

#endif // MAINWINDOW_H

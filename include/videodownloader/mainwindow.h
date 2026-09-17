#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QDateTime>
#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>

#include "browserdetect.h"
#include "downloaditem.h"
#include "helpdialog.h"
#include "loglevel.h"
#include "nativehost.h"

class AddVideosCard;
class DownloadQueue;
class LogView;
class QSettings;
class QueueView;
class TabHeader;
class ToolsManager;
class UpdateService;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // Capture: solo construye la interfaz, sin tools, cola, red ni settings del usuario.
    // La usa --ui-shot para dibujar estados de prueba sin efectos secundarios.
    enum class Mode { Normal, Capture };

    explicit MainWindow(Mode mode = Mode::Normal, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Servicios para la UI (ventana principal y dialogo de Help):
    //  - UpdateService: update de la app (estado, version disponible, installAppUpdate()).
    //  - ToolsManager: rutas y versiones de yt-dlp/deno/ffmpeg, auto-update de tools.
    //  - DownloadQueue: activeDownloadCount() para confirmar antes de instalar un update.
    UpdateService *updateService() const { return m_updateService; }
    ToolsManager *toolsManager() const { return m_toolsManager; }
    DownloadQueue *downloadQueue() const { return m_downloadQueue; }

    // Piezas de la ventana (la captura de QA las carga con datos de prueba).
    TabHeader *tabHeader() const { return m_tabHeader; }
    AddVideosCard *addCard() const { return m_addCard; }
    QueueView *queueView() const { return m_queueView; }
    LogView *logView() const { return m_logView; }

    void log(const QString &text, LogLevel level = LogLevel::Info);

    // Pedido de la extension de navegador (ya validado): encola el link con las opciones
    // actuales y la sesion que mando la extension. Devuelve la respuesta para el host.
    QJsonObject handleBrowserRequest(const NativeHost::Request &request);
    // Segunda apertura del exe: trae esta ventana al frente.
    void bringToFront();

    // <AppData>/LGA/VideoDownloader/config.ini (crea la carpeta si falta).
    static QString configPath();

    // Auto-update de tools y de la app al arrancar (activo por defecto). El recorrido de QA
    // aislado lo apaga para usar solo las tools del build.
    static void setAutomaticUpdatesEnabled(bool enabled);

private:
    void setupUi();
    void setupServices();
    void loadSettings();
    DownloadOptions currentOptions() const;
    static bool isValidDownloadPath(const QString &path);

    void onDownloadRequested();
    void onBrowseRequested();
    void onCookiesSourceActivated(const QString &key);
    void onToolsStatusChanged();
    void refreshCookiesAttention();
    void onVideoPasswordRequired(const DownloadItem &item);
    void onShowRequested(int id);
    void onCopyErrorRequested(int id);
    void onRetryRequested(int id);
    void onRetryWithBrowser(int id, const QString &browserKey);

    void refreshUpdateNotice();
    UpdateView currentUpdateView() const;
    void openHelp();
    void requestAppInstall();

    Mode m_mode;
    QSettings *m_settings = nullptr;
    QString m_captureSettingsPath;

    TabHeader *m_tabHeader = nullptr;
    AddVideosCard *m_addCard = nullptr;
    QueueView *m_queueView = nullptr;
    LogView *m_logView = nullptr;

    ToolsManager *m_toolsManager = nullptr;
    DownloadQueue *m_downloadQueue = nullptr;
    UpdateService *m_updateService = nullptr;
    QList<BrowserDetect::Browser> m_browsers;

    QPointer<HelpDialog> m_helpDialog;
    QDateTime m_lastUpdateCheck;
    qint64 m_installReceived = -1;
    qint64 m_installTotal = -1;
};

#endif // MAINWINDOW_H

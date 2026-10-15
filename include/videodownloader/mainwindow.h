#ifndef MAINWINDOW_H
#define MAINWINDOW_H

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
class LgaUpdateService;

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
    //  - LgaUpdateService: update de la app (libs/lgaupdate), con sus propios carteles.
    //  - ToolsManager: rutas y versiones de yt-dlp/deno/ffmpeg, auto-update de tools.
    //  - DownloadQueue: activeDownloadCount(), que posterga la instalacion de un update.
    LgaUpdateService *updateService() const { return m_updateService; }
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
    // Barrido de los restos de updates anteriores al arrancar (activo por defecto). Los modos de
    // QA lo apagan: las carpetas que barre las comparte la copia instalada.
    static void setUpdateCacheSweepEnabled(bool enabled);

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

    // Arma el LgaUpdateService con la identidad de esta app y lo engancha al header y a Help.
    void setupUpdates();
    // Motivo por el que no se puede instalar ahora (descargas en la cola), o vacio.
    QString updateInstallBlockedReason() const;
    void refreshUpdateNotice();
    UpdateView currentUpdateView() const;
    void openHelp();
    // Ofrece el update (cartel de lgaupdate con las notas), desde el header o desde Help.
    void offerAppUpdate();

    Mode m_mode;
    QSettings *m_settings = nullptr;
    QString m_captureSettingsPath;

    TabHeader *m_tabHeader = nullptr;
    AddVideosCard *m_addCard = nullptr;
    QueueView *m_queueView = nullptr;
    LogView *m_logView = nullptr;

    ToolsManager *m_toolsManager = nullptr;
    DownloadQueue *m_downloadQueue = nullptr;
    LgaUpdateService *m_updateService = nullptr;
    QList<BrowserDetect::Browser> m_browsers;

    QPointer<HelpDialog> m_helpDialog;
    // Lo que muestran el header y Help del update: la version nueva que encontro un chequeo
    // (vacio = ninguna) y si hay un chequeo en curso.
    QString m_availableVersion;
    bool m_updateChecking = false;
};

#endif // MAINWINDOW_H

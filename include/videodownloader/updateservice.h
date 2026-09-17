#ifndef UPDATESERVICE_H
#define UPDATESERVICE_H

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QCryptographicHash;
class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

// Auto-update de la APP (no de las tools), sin UI propia: la ventana principal o un dialogo
// consumen su estado y sus senales. Nunca muestra modales.
//
// - Fuente: GitHub Releases de `legandrop/LGA_VideoDownloader`, sin API. El tag sale del
//   redirect de `/releases/latest` y el hash del asset `SHA256SUMS` del MISMO tag
//   (obligatorio: sin hash no se ofrece el update). Sin releases -> UpToDate en silencio.
// - Windows: baja `VideoDownloader_Setup_v<version>.exe` en streaming, verifica el SHA-256
//   del manifiesto (fail-closed), corre el hook de cierre (cola + procesos hijos), lanza el
//   instalador con /SILENT y /DIR=<carpeta actual> y cierra la app; el instalador la relanza.
// - macOS: installAppUpdate() abre la pagina del release en el navegador.
// - Desde un build de desarrollo el chequeo funciona pero la instalacion se niega.
// Descarga e instalacion portadas de LGA_FolderSwitch/src/updates/UpdateService.cpp.
class UpdateService : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,            // todavia no se chequeo
        Checking,
        UpToDate,        // incluye "el repo no tiene releases"
        UpdateAvailable,
        CheckFailed,     // sin red, HTTP inesperado, SHA256SUMS ausente o sin el asset
        Downloading,     // bajando el instalador (Windows)
        Installing,      // instalador lanzado, la app se esta cerrando
        InstallFailed
    };
    Q_ENUM(State)

    explicit UpdateService(QObject *parent = nullptr);
    ~UpdateService() override;

    State state() const { return m_state; }
    QString currentVersion() const;
    // Version remota ofrecida (vacio si no hay update).
    QString availableVersion() const { return m_availableVersion; }
    // Pagina del release remoto (notas). Valida solo con UpdateAvailable.
    QUrl releasePageUrl() const { return m_releasePageUrl; }
    // Detalle del ultimo CheckFailed / InstallFailed, en ingles, apto para mostrar.
    QString errorString() const { return m_errorString; }
    // true si installAppUpdate() instala en el lugar (Windows); false = abre el release (macOS).
    static bool installsInPlace();
    // Motivo por el que no se puede instalar desde esta copia (build de desarrollo); vacio si se puede.
    QString installBlockedReason() const;

    // Se llama justo antes de lanzar el instalador: debe cortar la cola y matar yt-dlp y sus
    // hijos, porque el taskkill del instalador no los alcanza y bloquearian la copia.
    void setBeforeInstallHook(std::function<void()> hook) { m_beforeInstallHook = std::move(hook); }

public slots:
    // Chequeo asincronico; si ya hay uno o una instalacion en curso, no hace nada.
    void checkForUpdates();
    // NO pide confirmacion: si DownloadQueue::activeDownloadCount() > 0, confirmar antes.
    void installAppUpdate();
    // Cancela la descarga del instalador en curso.
    void cancelInstall();

signals:
    void stateChanged(UpdateService::State state);
    void appUpdateAvailable(const QString &version, const QUrl &notesUrl);
    void installProgress(qint64 received, qint64 total);

private:
    void setState(State state);
    void failInstall(const QString &message);
    void onLatestFinished();
    void onSumsFinished();
    void failCheck(const QString &message);
    static QString assetNameFor(const QString &version);
    void onDownloadReadyRead();
    void onDownloadFinished();
    void launchInstaller(const QString &installerPath);
    void discardPartialDownload();

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_checkReply = nullptr;
    QNetworkReply *m_downloadReply = nullptr;
    State m_state = State::Idle;
    QString m_errorString;

    QString m_checkTag;
    QString m_checkVersion;
    QString m_availableVersion;
    QString m_assetName;
    QString m_assetDigest;
    QUrl m_assetUrl;
    QUrl m_releasePageUrl;

    std::function<void()> m_beforeInstallHook;

    QSaveFile *m_downloadFile = nullptr;
    QCryptographicHash *m_downloadHash = nullptr;
    QString m_downloadTargetPath;
    bool m_downloadCancelled = false;
    bool m_downloadWriteFailed = false;
    bool m_downloadResponseChecked = false;
};

#endif // UPDATESERVICE_H

#ifndef UPDATESERVICE_H
#define UPDATESERVICE_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

#include "whatsnew.h"

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

    // Carpetas del instalador descargado. Windows: `<app>/updates`, o
    // `%LOCALAPPDATA%/LGA/VideoDownloader/updates` si la instalacion no es escribible
    // (AppPaths::heavyDataDir). Las versiones <= 0.95 lo bajaban a %TEMP%: esa carpeta se sigue
    // barriendo. Publicas para `--qa-update-dirs`, que las prueba con carpetas de prueba.
    static QString legacyTempUpdateDir();
    static QStringList installerSweepDirs(const QString &appDir, const QString &fallbackDir);
    static int removeOldInstallers(const QStringList &dirs, const QString &keepName);
    // Apaga el barrido automatico (arranque, reintento y antes de bajar) en todo el proceso. Lo
    // llaman los modos de QA, que no deben tocar las carpetas reales.
    static void disableAutoSweep();

    // Se llama justo antes de lanzar el instalador: debe cortar la cola y matar yt-dlp y sus
    // hijos, porque el instalador solo cierra VideoDownloader.exe y bloquearian la copia.
    void setBeforeInstallHook(std::function<void()> hook) { m_beforeInstallHook = std::move(hook); }

    // Notas para el usuario (What's new). El chequeo las pide solo al ofrecer un update; el
    // update se ofrece igual si no llegan. Missing = el release no trae `whats_new.json` (404).
    enum class NotesStatus { None, Loading, Ready, Missing, Failed };
    NotesStatus notesStatus() const { return m_notesStatus; }
    // Validas solo con Ready: el historial completo hasta el release pedido.
    const WhatsNew::Notes &notes() const { return m_notes; }
    // Baja `whats_new.json` del release `tag`. Sin hash: el archivo se genera despues del release
    // y viaja por el mismo HTTPS que SHA256SUMS; se valida estricto (WhatsNew::parse) y el texto
    // se escapa al armar el HTML. Si falla, se usa la cache si trae la version de `tag`. Emite
    // notesChanged() al terminar, tambien al fallar.
    void fetchNotes(const QString &tag);
    // Ultimas notas validas bajadas (`<updates>/whats_new.json`), o invalidas si no hay.
    static WhatsNew::Notes cachedNotes();
    // Carpeta de la cache: la de updates (AppPaths::heavyDataDir), salvo el override de QA.
    static QString notesCacheDir();
    static void setNotesCacheDirForQa(const QString &dir);

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
    void notesChanged();

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
    void sweepInstallers(const QString &keepName);
    void onNotesFinished();

    QNetworkAccessManager *m_network = nullptr;
    // QPointer: si la reply muere con el manager antes que este puntero se toque, queda en null
    // en vez de colgando.
    QPointer<QNetworkReply> m_notesReply;
    QByteArray m_notesBuffer;
    QString m_notesTag;
    bool m_notesTooLarge = false;
    NotesStatus m_notesStatus = NotesStatus::None;
    WhatsNew::Notes m_notes;
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

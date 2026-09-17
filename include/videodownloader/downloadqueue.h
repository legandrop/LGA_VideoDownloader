#ifndef DOWNLOADQUEUE_H
#define DOWNLOADQUEUE_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringDecoder>

#include "downloaditem.h"
#include "loglevel.h"

class ToolsManager;

// Cola de descargas sin widgets: guarda los items en orden, lanza yt-dlp de a uno y avisa
// cada cambio por senales. La UI (QueueView, LogView) solo escucha.
class DownloadQueue : public QObject
{
    Q_OBJECT

public:
    explicit DownloadQueue(ToolsManager *toolsManager, QObject *parent = nullptr);
    ~DownloadQueue();

    // Encola un link valido y devuelve su id. La cola arranca sola.
    int addDownload(const QString &url, const DownloadOptions &options);
    // Texto pegado sin ningun link: una unica tarjeta que lo explica.
    int addNoLinkFound(const QString &text);

    // Cancela el item: si es el actual mata yt-dlp, si esta en cola lo marca cancelado.
    void cancelItem(int id);
    // Saca de la lista un item que no esta descargando.
    void removeItem(int id);
    // Vuelve a encolar un item terminado (fallido o cancelado) con las cookies indicadas.
    void retryItem(int id, const DownloadOptions &options);
    void clearFinished();
    void cancelAll();

    // Reintenta el item actual con la contrasena de VIDEO (no de la cuenta).
    void retryDownloadWithVideoPassword(const QString &videoPassword);
    // El usuario no dio la contrasena: el item actual queda fallido y sigue la cola.
    void abandonPasswordRequest();

    // Arranca lo pendiente si no hay nada corriendo (por ejemplo cuando las tools quedan listas).
    void kick();

    QList<DownloadItem> items() const { return m_items; }
    const DownloadItem *item(int id) const;

    // Hay un proceso de yt-dlp vivo (el swap de tools tiene que esperar).
    bool hasActiveProcess() const;
    // Descargas que se perderian al cerrar: la actual mas las encoladas.
    int activeDownloadCount() const;
    // Para el update de la app: vacia la cola y mata yt-dlp CON sus hijos (ffmpeg, deno,
    // y el proceso real de yt-dlp, que en Windows es hijo del lanzador onefile).
    void stopAllForShutdown();

signals:
    void itemAdded(const DownloadItem &item);
    void itemUpdated(const DownloadItem &item);
    void itemRemoved(int id);
    void logLine(const QString &text, LogLevel level);
    void videoPasswordRequired(const DownloadItem &item);

private slots:
    void processNextDownload();
    void onDownloadFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onDownloadOutput();
    void onDownloadError();

private:
    DownloadItem *findItem(int id);
    int indexOf(int id) const;
    DownloadItem *currentItem();
    void startDownloadProcess(DownloadItem &item);
    void handleStdoutLine(const QString &line);
    void finishCurrent();
    // Mata yt-dlp si sigue vivo y, ya sin proceso, borra el cookies.txt temporal de la sesion.
    void cleanupCurrentProcess();
    // Mata yt-dlp CON sus hijos: el ejecutable onefile relanza el yt-dlp real como hijo, y
    // este a su vez lanza ffmpeg y deno. Matar solo el padre dejaba la descarga huerfana.
    void killCurrentProcessTree();
    void classifyFailure(DownloadItem &item) const;
    void flushStderrBuffer();
    void logStderrLine(const QString &line);
    // Transmision en vivo detectada en [vdinfo]: marca el item y corta yt-dlp.
    void abortLive(DownloadItem &item, bool upcoming);
    // Solo marca el item como vivo no soportado (cuando yt-dlp ya lo salteo por el filtro).
    void markLive(DownloadItem &item, bool upcoming);
    // Borra los parciales del item actual (cancelado, vivo abortado o cierre de la app).
    void removePartialFiles();
    void log(const QString &text, LogLevel level = LogLevel::Info);
    void emitUpdated(const DownloadItem &item, bool throttle = false);

    ToolsManager *m_toolsManager;
    QList<DownloadItem> m_items;
    int m_nextId = 1;
    int m_currentId = -1;
    bool m_waitingForTools = false;
    bool m_stopped = false;

    QProcess *m_currentProcess = nullptr;
    QString m_sessionCookiesFile;  // cookies.txt temporal del item actual (sesion de la extension)
    QString m_stdoutBuffer;  // linea incompleta de stdout pendiente del proximo chunk
    QString m_stderrBuffer;  // idem stderr
    QStringDecoder m_stdoutDecoder{QStringDecoder::Utf8};
    QStringDecoder m_stderrDecoder{QStringDecoder::Utf8};
    bool m_errorLogged = false;  // yt-dlp ya escribio una linea ERROR para el item actual
    bool m_liveAbort = false;    // se corto yt-dlp porque el link es una transmision en vivo
    int m_liveSkipped = 0;       // vivos que yt-dlp salteo por --match-filter
    QStringList m_destinations;  // rutas "[download] Destination:" del item actual
    QStringList m_formatIds;     // formatos elegidos ("137", "140") para reconocer streams intermedios
    QString m_mergeTarget;       // archivo final de la union de video + audio
    qint64 m_expectedTotal = 0;  // tamano anunciado del formato elegido (0 = desconocido)

    // Progreso de varios streams (video + audio se bajan por separado y despues se unen).
    int m_streamCount = 1;
    int m_streamIndex = 0;
    qint64 m_streamDoneBase = 0;   // bytes de los streams ya terminados
    qint64 m_lastStreamTotal = 0;
    QElapsedTimer m_progressThrottle;
};

#endif // DOWNLOADQUEUE_H

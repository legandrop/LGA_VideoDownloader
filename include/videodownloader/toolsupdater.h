#ifndef TOOLSUPDATER_H
#define TOOLSUPDATER_H

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

class QCryptographicHash;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QSaveFile;
class QTimer;

// Instala y actualiza yt-dlp y deno en la carpeta de datos del usuario, en silencio.
//
// - Fuente: GitHub directo, sin API. El tag se resuelve UNA vez leyendo el redirect de
//   `/releases/latest`; despues todo se baja del tag fijo, asi un release nuevo publicado
//   a mitad de camino no mezcla el binario de uno con las sumas de otro.
// - Integridad fail-closed: sin SHA-256 valido en el archivo de sumas no se instala nada.
// - Descarga en streaming a QSaveFile con hash incremental; nunca readAll() ni escritura
//   directa sobre el binario final.
// - Lo verificado queda en `<tools>/.staging` y se activa con applyStaged(), que es el
//   UNICO punto de swap: lo llaman el arranque y DownloadQueue antes de lanzar cada proceso.
// - Carpeta: Windows `%LOCALAPPDATA%/LGA/VideoDownloader/tools`,
//   macOS `~/Library/Application Support/LGA/VideoDownloader/tools`.
class ToolsUpdater : public QObject
{
    Q_OBJECT

public:
    enum class Tool { YtDlp, Deno };

    explicit ToolsUpdater(QObject *parent = nullptr);
    ~ToolsUpdater() override;

    // Carpeta de tools del usuario (no la crea).
    static QString toolsDir();
    // Nombre del binario final: "yt-dlp.exe"/"deno.exe" en Windows, sin extension en macOS.
    static QString binaryName(Tool tool);
    // Clave en tools.json y en los logs: "yt-dlp" / "deno".
    static QString toolKey(Tool tool);
    // Ruta del binario en la carpeta de usuario, o vacio si no existe.
    static QString installedBinary(Tool tool);
    // Version registrada en tools.json para el binario instalado; vacio si no hay.
    static QString installedVersion(Tool tool);

    // Swap de lo que haya verificado en staging. SOLO se llama sin procesos de yt-dlp
    // activos. Devuelve las tools reemplazadas y deja el detalle en logLines.
    static QStringList applyStaged(QStringList *logLines);
    // Al arrancar: borra `.old` de swaps anteriores y descargas a medio terminar.
    static void cleanupLeftovers();

    // Parser de sumas (publico para poder probarlo): extrae un SHA-256 de 64 hex sin
    // importar mayusculas. Con assetName, la linea "hash  nombre" cuyo nombre coincide
    // exacto (formato SHA2-256SUMS de yt-dlp); sin assetName, el primer hash del archivo
    // (el .sha256sum de deno, que en Windows es la salida de Get-FileHash). Vacio si no hay.
    static QString parseSha256(const QByteArray &sumsBody, const QString &assetName);

    bool isRunning() const { return m_running; }

    // Chequea e instala/actualiza yt-dlp y despues deno, en secuencia. Si ya corre, no hace nada.
    void start();

signals:
    // Linea para el log visible (en ingles, como el resto del log de la app).
    void logMessage(const QString &line);
    // Hay un binario verificado esperando el swap.
    void toolStaged(const QString &tool, const QString &version);
    void runningChanged(bool running);
    // ok = ninguna tool fallo (estar al dia cuenta como ok).
    void finished(bool ok);

private:
    enum class Phase { Idle, ResolveTag, Sums, Asset, Extract, Smoke };

    void startTool(Tool tool);
    void nextTool();
    void failTool(const QString &line);
    void onResolveFinished();
    void onSumsFinished();
    void onAssetReadyRead();
    void onAssetFinished();
    void startExtract();
    void startSmokeTest();
    void markStaged();
    void discardDownload();

    QString repoSlug(Tool tool) const;
    QString assetName(Tool tool) const;
    QString sumsFileName(Tool tool) const;
    QUrl githubUrl(const QString &path) const;

    QNetworkAccessManager *m_network = nullptr;
    QString m_githubBase;
    bool m_running = false;
    bool m_allOk = true;
    QList<Tool> m_pending;
    Tool m_tool = Tool::YtDlp;
    Phase m_phase = Phase::Idle;

    QString m_tag;
    QString m_version;
    QString m_expectedSha;
    QNetworkReply *m_reply = nullptr;
    QSaveFile *m_file = nullptr;
    QCryptographicHash *m_hash = nullptr;
    bool m_statusChecked = false;
    bool m_writeFailed = false;
    QString m_downloadPath;
    QPointer<QProcess> m_process;
};

#endif // TOOLSUPDATER_H

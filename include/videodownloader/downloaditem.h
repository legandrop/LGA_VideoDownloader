#ifndef DOWNLOADITEM_H
#define DOWNLOADITEM_H

#include <QString>
#include <QDateTime>

enum class DownloadStatus {
    Pending,
    Downloading,
    Completed,
    Failed,
    Cancelled
};

// Que archivo se pide: video MP4 (video + audio unidos) o solo el audio en M4A.
enum class OutputFormat {
    VideoMp4,
    AudioM4a
};

// Calidad del video. Compatible prioriza H.264 (abre en cualquier editor); Best toma la
// resolucion mas alta aunque venga en AV1/VP9.
enum class VideoQuality {
    Compatible,
    Best
};

// Motivo de un fallo, traducido de la salida de yt-dlp para mostrar una solucion concreta.
enum class FailureKind {
    None,
    NeedsSignIn,        // edad, privado, miembros, Vimeo con sesion
    CookiesUnreadable,  // el navegador elegido no deja leer sus cookies
    InvalidLink,        // yt-dlp no soporta el sitio del link
    Unavailable,        // video borrado o inexistente
    Network,
    ToolsMissing,
    PasswordRequired,
    LiveStream,         // transmision en vivo: no se puede bajar, reintentar no sirve
    NoLinkFound,        // texto pegado sin ningun link
    Generic
};

// Opciones con las que se encola un link. Las cookies: como mucho uno de los dos cargado.
struct DownloadOptions {
    QString cookiesBrowser;  // nombre que entiende --cookies-from-browser (firefox, chrome, ...)
    QString cookiesFile;     // ruta a un cookies.txt en formato Netscape (--cookies)
    QString downloadDir;
    OutputFormat format = OutputFormat::VideoMp4;
    VideoQuality quality = VideoQuality::Compatible;
};

struct DownloadItem {
    int id = 0;
    QString url;
    DownloadOptions options;
    QString videoPassword;

    DownloadStatus status = DownloadStatus::Pending;
    QDateTime addedTime = QDateTime::currentDateTime();
    QDateTime startTime;
    QDateTime finishTime;

    // Datos que llegan de yt-dlp mientras descarga (vacios o -1 hasta que se conocen).
    QString title;
    QString extractor;       // extractor_key de yt-dlp ("Dailymotion", "Generic"...)
    QString resolution;      // "1920x1080"
    QString extension;       // "mp4" / "m4a"
    QString filePath;        // archivo final, despues de unir y mover
    int progress = 0;        // 0..100, total de todos los streams
    qint64 totalBytes = -1;  // suma estimada de los streams
    qint64 doneBytes = -1;
    double speedBytes = -1;  // bytes por segundo
    int etaSeconds = -1;
    bool finishing = false;  // streams bajados, ffmpeg uniendo o convirtiendo

    // Fallo: salida cruda de yt-dlp (stderr) y la version entendible para la tarjeta.
    QString errorMessage;
    FailureKind failure = FailureKind::None;
    QString errorHeadline;
    QString errorDetail;

    bool isFinished() const {
        return status == DownloadStatus::Completed ||
               status == DownloadStatus::Failed ||
               status == DownloadStatus::Cancelled;
    }

    // Fallos que se arreglan reintentando (con otra sesion, otra red, etc.).
    bool isRetryable() const {
        return failure != FailureKind::InvalidLink && failure != FailureKind::LiveStream
            && failure != FailureKind::NoLinkFound;
    }

    bool isYouTube() const {
        return url.contains(QLatin1String("youtube.com"), Qt::CaseInsensitive)
            || url.contains(QLatin1String("youtu.be"), Qt::CaseInsensitive);
    }

    bool isVimeo() const {
        return url.contains(QLatin1String("vimeo.com"), Qt::CaseInsensitive);
    }
};

#endif // DOWNLOADITEM_H

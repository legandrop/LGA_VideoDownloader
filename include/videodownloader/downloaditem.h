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

struct DownloadItem {
    QString url;
    // Autenticacion por cookies, nunca por usuario/contrasena de la cuenta.
    // cookiesBrowser: nombre que entiende --cookies-from-browser (firefox, chrome, ...).
    // cookiesFile: ruta a un cookies.txt en formato Netscape (--cookies).
    // Como mucho uno de los dos viene cargado.
    QString cookiesBrowser;
    QString cookiesFile;
    QString videoPassword;
    QString downloadDir;
    QString title;
    DownloadStatus status;
    QDateTime addedTime;
    QDateTime startTime;
    QDateTime finishTime;
    int progress;
    QString errorMessage;
    
    DownloadItem()
        : status(DownloadStatus::Pending)
        , addedTime(QDateTime::currentDateTime())
        , progress(0)
    {}

    DownloadItem(const QString &url, const QString &browser, const QString &cookiesTxt, const QString &dir)
        : url(url)
        , cookiesBrowser(browser)
        , cookiesFile(cookiesTxt)
        , videoPassword("")
        , downloadDir(dir)
        , status(DownloadStatus::Pending)
        , addedTime(QDateTime::currentDateTime())
        , progress(0)
    {}

    DownloadItem(const QString &url, const QString &browser, const QString &cookiesTxt, const QString &videoPass, const QString &dir)
        : url(url)
        , cookiesBrowser(browser)
        , cookiesFile(cookiesTxt)
        , videoPassword(videoPass)
        , downloadDir(dir)
        , status(DownloadStatus::Pending)
        , addedTime(QDateTime::currentDateTime())
        , progress(0)
    {}
    
    bool isFinished() const {
        return status == DownloadStatus::Completed || 
               status == DownloadStatus::Failed || 
               status == DownloadStatus::Cancelled;
    }
    
    QString getStatusString() const {
        switch (status) {
            case DownloadStatus::Pending: return "Pending";
            case DownloadStatus::Downloading: return "Downloading";
            case DownloadStatus::Completed: return "Completed";
            case DownloadStatus::Failed: return "Failed";
            case DownloadStatus::Cancelled: return "Cancelled";
            default: return "Unknown";
        }
    }
};

#endif // DOWNLOADITEM_H

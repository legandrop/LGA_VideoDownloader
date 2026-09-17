#include "videodownloader/sessioncookies.h"
#include "videodownloader/apppaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QThread>

#include <cmath>

namespace {

bool hasControlChars(const QString &text)
{
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\t') || ch == QLatin1Char('\r') || ch == QLatin1Char('\n') || ch == QChar(0)) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace SessionCookies {

QByteArray toNetscape(const QJsonArray &cookies, int *accepted)
{
    // Cabecera que yt-dlp (MozillaCookieJar) espera en la primera linea.
    QByteArray text("# Netscape HTTP Cookie File\n# Temporary file written by LGA Video Downloader.\n\n");
    int count = 0;
    for (const QJsonValue &entry : cookies) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject cookie = entry.toObject();
        QString domain = cookie.value(QStringLiteral("domain")).toString();
        QString path = cookie.value(QStringLiteral("path")).toString();
        const QString name = cookie.value(QStringLiteral("name")).toString();
        const QString value = cookie.value(QStringLiteral("value")).toString();
        if (hasControlChars(domain) || hasControlChars(path) || hasControlChars(name) || hasControlChars(value)) {
            continue;
        }
        while (domain.startsWith(QLatin1Char('.'))) {
            domain.remove(0, 1);
        }
        if (domain.isEmpty()) {
            continue;
        }
        if (path.isEmpty()) {
            path = QStringLiteral("/");
        }
        // El punto inicial y el flag de subdominios siempre coinciden.
        const bool hostOnly = cookie.value(QStringLiteral("hostOnly")).toBool(false);
        if (!hostOnly) {
            domain.prepend(QLatin1Char('.'));
        }
        qint64 expires = 0;
        const QJsonValue expiration = cookie.value(QStringLiteral("expirationDate"));
        if (expiration.isDouble() && std::isfinite(expiration.toDouble()) && expiration.toDouble() > 0) {
            // Tope en 9999-12-31 para no desbordar el entero.
            expires = qint64(std::floor(qMin(expiration.toDouble(), 253402300799.0)));
        }

        QByteArray line;
        if (cookie.value(QStringLiteral("httpOnly")).toBool(false)) {
            line += "#HttpOnly_";
        }
        line += domain.toUtf8();
        line += hostOnly ? "\tFALSE\t" : "\tTRUE\t";
        line += path.toUtf8();
        line += cookie.value(QStringLiteral("secure")).toBool(false) ? "\tTRUE\t" : "\tFALSE\t";
        line += QByteArray::number(expires);
        line += '\t';
        line += name.toUtf8();
        line += '\t';
        line += value.toUtf8();
        line += '\n';
        text += line;
        ++count;
    }
    if (accepted) {
        *accepted = count;
    }
    return text;
}

QString directory()
{
    // Windows: `<app>/session-cookies` (fallback a %LOCALAPPDATA%). macOS: Application Support.
    return AppPaths::heavyDataDir(QStringLiteral("session-cookies"));
}

QString writeTempFile(const QByteArray &content)
{
    const QString dir = directory();
    if (!QDir().mkpath(dir)) {
        return QString();
    }
    // QTemporaryFile crea el archivo en exclusiva, con nombre aleatorio y permisos 0600 en Unix;
    // en Windows hereda la ACL de la carpeta que lo contiene.
    QTemporaryFile file(QDir(dir).filePath(QStringLiteral("XXXXXXXXXXXX.txt")));
    file.setAutoRemove(false);
    if (!file.open()) {
        return QString();
    }
    const QString path = file.fileName();
    const bool written = file.write(content) == content.size() && file.flush();
    // Cerrado ANTES de lanzar yt-dlp: en Windows un handle abierto le impide reescribirlo.
    file.close();
    if (!written) {
        QFile::remove(path);
        return QString();
    }
    return path;
}

bool removeFile(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return true;
    }
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (QFile::remove(path)) {
            return true;
        }
        QThread::msleep(150);
    }
    return !QFileInfo::exists(path);
}

int sweep()
{
    const QDir dir(directory());
    if (!dir.exists()) {
        return 0;
    }
    int removed = 0;
    const QStringList entries = dir.entryList(QDir::Files | QDir::Hidden | QDir::System);
    for (const QString &entry : entries) {
        if (QFile::remove(dir.filePath(entry))) {
            ++removed;
        }
    }
    return removed;
}

} // namespace SessionCookies

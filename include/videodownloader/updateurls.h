#ifndef UPDATEURLS_H
#define UPDATEURLS_H

#include <QDebug>
#include <QString>
#include <QUrl>
#include <QtGlobal>

// URLs de actualizacion con override SOLO local, para probar sin publicar nada:
//   LGA_VD_GITHUB_BASE   reemplaza https://github.com (tools y update de la app)
// Cualquier host que no sea localhost/127.0.0.1 se ignora: en produccion la variable no
// puede desviar las descargas a otro servidor.
namespace UpdateUrls {

inline bool isLocalOverride(const QUrl &url)
{
    const QString host = url.host().toLower();
    const QString scheme = url.scheme().toLower();
    return url.isValid() && (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
           && (host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost"));
}

inline QString localOverride(const char *envName)
{
    const QString raw = qEnvironmentVariable(envName).trimmed();
    if (raw.isEmpty()) {
        return QString();
    }
    if (!isLocalOverride(QUrl(raw))) {
        qWarning() << "[Updates] Override ignorado (solo localhost/127.0.0.1):" << envName << raw;
        return QString();
    }
    return raw;
}

// Sin barra final.
inline QString githubBase()
{
    QString base = localOverride("LGA_VD_GITHUB_BASE");
    if (base.isEmpty()) {
        base = QStringLiteral("https://github.com");
    }
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return base;
}

} // namespace UpdateUrls

#endif // UPDATEURLS_H

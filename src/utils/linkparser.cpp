#include "videodownloader/linkparser.h"

#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace {

struct Site {
    const char *domain;
    const char *name;
};

// Sitios con nombre propio en la tarjeta. El resto funciona igual (lo decide yt-dlp).
const Site kSites[] = {
    {"youtube.com", "YouTube"},   {"youtu.be", "YouTube"},       {"vimeo.com", "Vimeo"},
    {"soundcloud.com", "SoundCloud"}, {"instagram.com", "Instagram"}, {"tiktok.com", "TikTok"},
    {"facebook.com", "Facebook"}, {"fb.watch", "Facebook"},      {"x.com", "X"},
    {"twitter.com", "X"},         {"twitch.tv", "Twitch"},       {"dailymotion.com", "Dailymotion"},
    {"reddit.com", "Reddit"},     {"redd.it", "Reddit"},         {"bilibili.com", "Bilibili"},
};

bool hostMatches(const QString &host, const char *domain)
{
    const QLatin1String d(domain);
    return host == d || host.endsWith(QLatin1Char('.') + QString(d));
}

// Dominio conocido escrito sin esquema ("youtube.com/watch?v=..."): se acepta como link.
bool startsWithKnownDomain(const QString &token)
{
    const QString host = token.section(QLatin1Char('/'), 0, 0).toLower();
    for (const Site &site : kSites) {
        if (hostMatches(host, site.domain)) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace LinkParser {

QString siteName(const QString &url)
{
    const QString host = QUrl(url).host().toLower();
    for (const Site &site : kSites) {
        if (hostMatches(host, site.domain)) {
            return QString::fromLatin1(site.name);
        }
    }
    return QString();
}

Result parse(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("[\\s,;]+"));
    Result result;
    QSet<QString> seen;
    QStringList ignored;
    const QStringList tokens = text.split(separators, Qt::SkipEmptyParts);
    for (QString token : tokens) {
        // Comillas, parentesis o un punto final de haberlo copiado dentro de una frase.
        while (!token.isEmpty() && QStringLiteral("<>\"'()[]").contains(token.front())) {
            token.remove(0, 1);
        }
        while (!token.isEmpty() && QStringLiteral("<>\"'()[].").contains(token.back())) {
            token.chop(1);
        }
        if (token.isEmpty()) {
            continue;
        }
        const bool hasScheme = token.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
                               || token.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
        if (!hasScheme && (startsWithKnownDomain(token) || token.startsWith(QLatin1String("www."), Qt::CaseInsensitive))) {
            token.prepend(QStringLiteral("https://"));
        } else if (!hasScheme) {
            ignored.append(token);
            continue;
        }
        const QUrl url(token, QUrl::StrictMode);
        // Un host sin punto ("https://hola") no es un sitio.
        if (!url.isValid() || !url.host().contains(QLatin1Char('.'))) {
            ignored.append(token);
            continue;
        }
        if (seen.contains(token)) {
            continue;
        }
        seen.insert(token);
        result.links.append(token);
    }
    result.ignoredWords = ignored.size();
    result.ignoredText = ignored.join(QLatin1Char(' '));
    return result;
}

} // namespace LinkParser

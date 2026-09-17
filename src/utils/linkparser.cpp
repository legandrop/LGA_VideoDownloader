#include "videodownloader/linkparser.h"

#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

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

// Host con TLD seguido de un path ("archive.org/details/foo"): link sin esquema de cualquier
// sitio. Sin "/" no cuenta, para no tomar palabras sueltas como "hola.que".
bool looksLikeSchemelessUrl(const QString &token)
{
    static const QRegularExpression pattern(QStringLiteral("^(?:[a-z0-9-]+\\.)+[a-z]{2,}/\\S*$"),
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern.match(token).hasMatch();
}

// Puntuacion pegada de haber copiado el link dentro de una frase: se recorta al final, y
// ")" o "]" solo si no tienen su apertura dentro del link (un "(1)" de Wikipedia se queda).
QString trimPunctuation(QString token)
{
    while (!token.isEmpty() && QStringLiteral("<\"'([").contains(token.front())) {
        token.remove(0, 1);
    }
    while (!token.isEmpty()) {
        const QChar last = token.back();
        if (QStringLiteral(".,!?:;>\"'").contains(last)
            || (last == QLatin1Char(')') && token.count(QLatin1Char('(')) < token.count(QLatin1Char(')')))
            || (last == QLatin1Char(']') && token.count(QLatin1Char('[')) < token.count(QLatin1Char(']')))) {
            token.chop(1);
        } else {
            break;
        }
    }
    return token;
}

// Clave para no repetir el mismo video pegado con dos formas de link.
QString dedupeKey(const QUrl &url, const QString &token)
{
    const QString host = url.host().toLower();
    const QStringList path = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (hostMatches(host, "youtu.be") && !path.isEmpty()) {
        return QStringLiteral("youtube:") + path.first();
    }
    if (hostMatches(host, "youtube.com")) {
        const QString v = QUrlQuery(url).queryItemValue(QStringLiteral("v"));
        if (!v.isEmpty()) {
            return QStringLiteral("youtube:") + v;
        }
        if (path.size() >= 2 && QStringList{QStringLiteral("shorts"), QStringLiteral("live"), QStringLiteral("embed")}
                                    .contains(path.first())) {
            return QStringLiteral("youtube:") + path.at(1);
        }
    }
    if (hostMatches(host, "vimeo.com")) {
        static const QRegularExpression digits(QStringLiteral("^\\d+$"));
        for (const QString &part : path) {
            if (digits.match(part).hasMatch()) {
                return QStringLiteral("vimeo:") + part;
            }
        }
    }
    return token;
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
    // Espacios, saltos y ";" siempre separan. La coma solo separa si la sigue otro link
    // (esquema, "www." o host con TLD y "/"): "?ids=1,2,3" queda entero.
    static const QRegularExpression separators(QStringLiteral("[\\s;]+"));
    static const QRegularExpression commaBeforeLink(
        QStringLiteral(",(?=https?://|www\\.|(?:[a-z0-9-]+\\.)+[a-z]{2,}/)"), QRegularExpression::CaseInsensitiveOption);
    Result result;
    QSet<QString> seen;
    QStringList ignored;
    QStringList tokens;
    for (const QString &chunk : text.split(separators, Qt::SkipEmptyParts)) {
        tokens << chunk.split(commaBeforeLink, Qt::SkipEmptyParts);
    }
    for (QString token : std::as_const(tokens)) {
        token = trimPunctuation(token);
        if (token.isEmpty()) {
            continue;
        }
        const bool hasScheme = token.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
                               || token.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
        if (!hasScheme && (startsWithKnownDomain(token) || token.startsWith(QLatin1String("www."), Qt::CaseInsensitive)
                           || looksLikeSchemelessUrl(token))) {
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
        const QString key = dedupeKey(url, token);
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        result.links.append(token);
    }
    result.ignoredWords = ignored.size();
    result.ignoredText = ignored.join(QLatin1Char(' '));
    return result;
}

} // namespace LinkParser

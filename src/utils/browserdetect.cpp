#include "videodownloader/browserdetect.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

struct Candidate {
    const char *key;
    const char *name;
    QStringList profileDirs;
};

bool anyExists(const QStringList &dirs)
{
    for (const QString &dir : dirs) {
        if (QFileInfo(dir).isDir()) {
            return true;
        }
    }
    return false;
}

QList<Candidate> candidates()
{
    const QString home = QDir::homePath();
#ifdef Q_OS_WIN
    const QString roaming = qEnvironmentVariable("APPDATA", home + QStringLiteral("/AppData/Roaming"));
    const QString local = qEnvironmentVariable("LOCALAPPDATA", home + QStringLiteral("/AppData/Local"));
    return {
        {"firefox", "Firefox", {roaming + "/Mozilla/Firefox/Profiles"}},
        {"chrome", "Chrome", {local + "/Google/Chrome/User Data"}},
        {"edge", "Edge", {local + "/Microsoft/Edge/User Data"}},
        {"brave", "Brave", {local + "/BraveSoftware/Brave-Browser/User Data"}},
        {"opera", "Opera", {roaming + "/Opera Software/Opera Stable", roaming + "/Opera Software/Opera GX Stable"}},
        {"vivaldi", "Vivaldi", {local + "/Vivaldi/User Data"}},
    };
#elif defined(Q_OS_MAC)
    const QString support = home + QStringLiteral("/Library/Application Support");
    return {
        {"safari", "Safari", {QStringLiteral("/Applications/Safari.app"), home + "/Library/Containers/com.apple.Safari"}},
        {"firefox", "Firefox", {support + "/Firefox/Profiles"}},
        {"chrome", "Chrome", {support + "/Google/Chrome"}},
        {"edge", "Edge", {support + "/Microsoft Edge"}},
        {"brave", "Brave", {support + "/BraveSoftware/Brave-Browser"}},
        {"opera", "Opera", {support + "/com.operasoftware.Opera"}},
        {"vivaldi", "Vivaldi", {support + "/Vivaldi"}},
    };
#else
    const QString config = home + QStringLiteral("/.config");
    return {
        {"firefox", "Firefox", {home + "/.mozilla/firefox", home + "/snap/firefox/common/.mozilla/firefox"}},
        {"chrome", "Chrome", {config + "/google-chrome"}},
        {"chromium", "Chromium", {config + "/chromium"}},
        {"edge", "Edge", {config + "/microsoft-edge"}},
        {"brave", "Brave", {config + "/BraveSoftware/Brave-Browser"}},
        {"opera", "Opera", {config + "/opera"}},
        {"vivaldi", "Vivaldi", {config + "/vivaldi"}},
    };
#endif
}

} // namespace

namespace BrowserDetect {

bool isReadableOnThisPlatform(const QString &key)
{
#ifdef Q_OS_WIN
    return key == QLatin1String("firefox");
#else
    Q_UNUSED(key)
    return true;
#endif
}

QList<Browser> detectInstalled()
{
    QList<Browser> result;
    for (const Candidate &candidate : candidates()) {
        if (!anyExists(candidate.profileDirs)) {
            continue;
        }
        const QString key = QString::fromLatin1(candidate.key);
        bool recommended = false;
#ifdef Q_OS_WIN
        recommended = key == QLatin1String("firefox");
#endif
        result.append({key, QString::fromLatin1(candidate.name), isReadableOnThisPlatform(key), recommended});
    }
    return result;
}

QString displayName(const QString &key)
{
    if (key.isEmpty()) {
        return QStringLiteral("the browser");
    }
    for (const Candidate &candidate : candidates()) {
        if (key == QLatin1String(candidate.key)) {
            return QString::fromLatin1(candidate.name);
        }
    }
    QString name = key;
    name[0] = name.at(0).toUpper();
    return name;
}

} // namespace BrowserDetect

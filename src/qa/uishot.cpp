#include "videodownloader/uishot.h"
#include "videodownloader/addvideoscard.h"
#include "videodownloader/helpdialog.h"
#include "videodownloader/logview.h"
#include "videodownloader/mainwindow.h"
#include "videodownloader/queueview.h"
#include "videodownloader/tabheader.h"

#include "videodownloader/apppaths.h"
#include "videodownloader/downloadqueue.h"
#include "videodownloader/linkparser.h"
#include "videodownloader/nativehost.h"
#include "videodownloader/pecheck.h"
#include "videodownloader/sessioncookies.h"
#include "videodownloader/toolsmanager.h"
#include "videodownloader/toolsupdater.h"
#include "videodownloader/updateservice.h"
#include "videodownloader/updateurls.h"
#include "videodownloader/whatsnew.h"
#include "videodownloader/whatsnewdialog.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <QFileInfo>
#include <QFontInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPixmap>
#include <QSaveFile>

// Captura de QA sin escritorio: construye la ventana real en modo Capture, la carga con
// datos de prueba (fixture, no descargas reales) y la dibuja a un PNG con QWidget::render.
// Nunca llama show() sobre una ventana de nivel superior, asi que no aparece nada en
// pantalla ni se toma el foco.

namespace {

const QStringList kStates = {
    QStringLiteral("empty"), QStringLiteral("downloading"), QStringLiteral("error"), QStringLiteral("tools"),
    QStringLiteral("help"), QStringLiteral("help-update"), QStringLiteral("help-downloading"),
    QStringLiteral("other-errors"), QStringLiteral("help-update-notes"), QStringLiteral("help-history"),
    QStringLiteral("after-install"),
};

// Notas de prueba para los estados que las muestran: `--notes <whats_new.json>`.
WhatsNew::Notes fixtureNotes(const QStringList &args)
{
    QFile file(args.value(args.indexOf(QStringLiteral("--notes")) + 1));
    if (!args.contains(QStringLiteral("--notes")) || !file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return WhatsNew::parse(file.readAll(), QStringLiteral("legandrop/LGA_VideoDownloader"));
}

constexpr int SHOT_WIDTH = 1200;
constexpr int SHOT_HEIGHT = 748;

DownloadOptions fixtureOptions(const QString &cookies)
{
    DownloadOptions options;
    options.downloadDir = QStringLiteral("D:/Downloads/Video");
    options.cookiesBrowser = cookies;
    return options;
}

DownloadItem fixtureItem(int id, const QString &url, DownloadStatus status, const QString &title)
{
    DownloadItem item;
    item.id = id;
    item.url = url;
    item.status = status;
    item.title = title;
    item.options = fixtureOptions(QStringLiteral("firefox"));
    return item;
}

void addLog(LogView *log, const char *time, LogLevel level, const QString &text)
{
    log->append(text, level, QTime::fromString(QLatin1String(time), QStringLiteral("HH:mm:ss")));
}

QList<BrowserDetect::Browser> fixtureBrowsers()
{
#ifdef Q_OS_WIN
    return {{QStringLiteral("firefox"), QStringLiteral("Firefox"), true, true},
            {QStringLiteral("chrome"), QStringLiteral("Chrome"), false, false},
            {QStringLiteral("edge"), QStringLiteral("Edge"), false, false}};
#else
    return {{QStringLiteral("safari"), QStringLiteral("Safari"), true, false},
            {QStringLiteral("firefox"), QStringLiteral("Firefox"), true, false},
            {QStringLiteral("chrome"), QStringLiteral("Chrome"), true, false}};
#endif
}

void loadDownloading(MainWindow &window)
{
    QueueView *queue = window.queueView();
    DownloadItem done = fixtureItem(1, QStringLiteral("https://vimeo.com/76979871"), DownloadStatus::Completed,
                                    QStringLiteral("Color Grading Session – Part 2"));
    done.totalBytes = 412LL * 1024 * 1024;
    done.resolution = QStringLiteral("1920x1080");
    done.extension = QStringLiteral("mp4");
    done.progress = 100;
    queue->upsertItem(done);

    DownloadItem running = fixtureItem(2, QStringLiteral("https://www.youtube.com/watch?v=jNQXAC9IVRw"),
                                       DownloadStatus::Downloading, QStringLiteral("Studio Tour 2026"));
    running.progress = 62;
    running.doneBytes = 734LL * 1024 * 1024;
    running.totalBytes = qint64(1.18 * 1024 * 1024 * 1024);
    running.speedBytes = 18.4 * 1024 * 1024;
    running.etaSeconds = 26;
    running.resolution = QStringLiteral("2560x1440");
    running.extension = QStringLiteral("mp4");
    queue->upsertItem(running);

    queue->upsertItem(fixtureItem(3, QStringLiteral("https://vimeo.com/123456789"), DownloadStatus::Pending,
                                  QStringLiteral("Director Interview – Final Cut")));
    queue->upsertItem(fixtureItem(4, QStringLiteral("https://www.youtube.com/watch?v=aB3xK9"), DownloadStatus::Pending,
                                  QStringLiteral("Lighting Breakdown")));

    LogView *log = window.logView();
    addLog(log, "10:41:15", LogLevel::Info, QStringLiteral("Added 4 links to the queue"));
    addLog(log, "10:41:16", LogLevel::Info, QStringLiteral("Fetching info · https://vimeo.com/76979871 · cookies: the Firefox session"));
    addLog(log, "10:41:18", LogLevel::Info, QStringLiteral("Format: 1920x1080 mp4 · Color Grading Session – Part 2"));
    addLog(log, "10:43:02", LogLevel::Done, QStringLiteral("Saved Color_Grading_Session_Part_2.mp4 (412 MiB)"));
    addLog(log, "10:43:03", LogLevel::Info, QStringLiteral("Fetching info · https://www.youtube.com/watch?v=jNQXAC9IVRw · cookies: the Firefox session"));
    addLog(log, "10:43:05", LogLevel::Warning, QStringLiteral("WARNING: [youtube] No H.264 format at 2160p; using 2560x1440"));
    addLog(log, "10:43:06", LogLevel::Info, QStringLiteral("Format: 2560x1440 mp4 · Studio Tour 2026"));
}

void loadError(MainWindow &window)
{
    QueueView *queue = window.queueView();
    DownloadItem failed = fixtureItem(1, QStringLiteral("https://www.youtube.com/watch?v=aB3xK9"), DownloadStatus::Failed, QString());
    failed.options.cookiesBrowser.clear();
    failed.failure = FailureKind::NeedsSignIn;
    failed.errorHeadline = QStringLiteral("Sign in to confirm your age");
    failed.errorDetail = QStringLiteral("No browser session was used. Sign in to YouTube in Firefox, pick Firefox in "
                                        "Use cookies from and retry.");
    failed.errorMessage = QStringLiteral("ERROR: [youtube] aB3xK9: Sign in to confirm your age.");
    queue->upsertItem(failed);

    DownloadItem done = fixtureItem(2, QStringLiteral("https://vimeo.com/76979871"), DownloadStatus::Completed,
                                    QStringLiteral("Director Interview – Final Cut"));
    done.options.cookiesBrowser.clear();
    done.totalBytes = 268LL * 1024 * 1024;
    done.resolution = QStringLiteral("1920x1080");
    done.extension = QStringLiteral("mp4");
    done.progress = 100;
    queue->upsertItem(done);

    LogView *log = window.logView();
    addLog(log, "10:52:30", LogLevel::Info, QStringLiteral("Added 2 links to the queue"));
    addLog(log, "10:52:31", LogLevel::Info, QStringLiteral("Fetching info · https://www.youtube.com/watch?v=aB3xK9 · cookies: no browser session"));
    addLog(log, "10:52:33", LogLevel::Error, QStringLiteral("ERROR: [youtube] aB3xK9: Sign in to confirm your age. This video may be inappropriate for some users."));
    addLog(log, "10:52:33", LogLevel::Detail, QStringLiteral("Sign in to confirm your age · No browser session was used. Sign in to YouTube in Firefox, pick Firefox in Use cookies from and retry."));
    addLog(log, "10:52:34", LogLevel::Info, QStringLiteral("Fetching info · https://vimeo.com/76979871 · cookies: no browser session"));
    addLog(log, "10:54:10", LogLevel::Done, QStringLiteral("Saved Director_Interview_Final_Cut.mp4 (268 MiB)"));
}

void loadOtherErrors(MainWindow &window)
{
    // Texto sin links, transmision en vivo y descarga cancelada (tanda de pruebas reales 0.93).
    QueueView *queue = window.queueView();
    DownloadItem noLink = fixtureItem(1, QStringLiteral("esto no es un link"), DownloadStatus::Failed, QString());
    noLink.failure = FailureKind::NoLinkFound;
    noLink.errorHeadline = QStringLiteral("No link found");
    noLink.errorDetail = QStringLiteral("Copy the address of the video (it starts with https://) and paste it again.");
    queue->upsertItem(noLink);

    DownloadItem live = fixtureItem(2, QStringLiteral("https://www.youtube.com/watch?v=jfKfPfyJRdk"), DownloadStatus::Failed,
                                    QStringLiteral("lofi hip hop radio - beats to relax/study to"));
    live.failure = FailureKind::LiveStream;
    live.errorHeadline = QStringLiteral("Live streams aren't supported");
    live.errorDetail = QStringLiteral("Only regular videos can be downloaded. If the stream is saved as a video when it ends, paste that link.");
    queue->upsertItem(live);

    DownloadItem cancelled = fixtureItem(3, QStringLiteral("https://www.youtube.com/watch?v=aqz-KE-bpKQ"), DownloadStatus::Cancelled,
                                         QStringLiteral("Big Buck Bunny 60fps 4K - Official Blender Foundation Short Film"));
    queue->upsertItem(cancelled);

    // Chips de sitio: conocido por dominio, extractor de yt-dlp y sitio no soportado.
    DownloadItem sound = fixtureItem(4, QStringLiteral("https://soundcloud.com/artist/track"), DownloadStatus::Completed,
                                     QStringLiteral("Short Public Track"));
    sound.totalBytes = 3LL * 1024 * 1024;
    sound.extension = QStringLiteral("mp3");
    sound.progress = 100;
    queue->upsertItem(sound);
    DownloadItem archive = fixtureItem(5, QStringLiteral("https://archive.org/details/example"), DownloadStatus::Completed,
                                       QStringLiteral("Public Domain Film"));
    archive.extractor = QStringLiteral("ArchiveOrg");
    archive.totalBytes = 21LL * 1024 * 1024;
    archive.resolution = QStringLiteral("640x480");
    archive.extension = QStringLiteral("mp4");
    archive.progress = 100;
    queue->upsertItem(archive);
    DownloadItem unsupported = fixtureItem(6, QStringLiteral("https://example.com"), DownloadStatus::Failed, QString());
    unsupported.extractor = QStringLiteral("Generic");
    unsupported.failure = FailureKind::InvalidLink;
    unsupported.errorHeadline = QStringLiteral("This site isn't supported");
    unsupported.errorDetail = QStringLiteral("There's no downloadable video at this link. Check the link or try the video's own page.");
    queue->upsertItem(unsupported);

    LogView *log = window.logView();
    addLog(log, "11:02:10", LogLevel::Warning, QStringLiteral("No link found in the pasted text"));
    addLog(log, "11:02:31", LogLevel::Info, QStringLiteral("Format: 1280x720 mp4 · lofi hip hop radio - beats to relax/study to"));
    addLog(log, "11:02:31", LogLevel::Error, QStringLiteral("Live streams aren't supported · https://www.youtube.com/watch?v=jfKfPfyJRdk"));
    addLog(log, "11:03:12", LogLevel::Warning, QStringLiteral("Cancelled Big Buck Bunny 60fps 4K - Official Blender Foundation Short Film"));
    addLog(log, "11:03:13", LogLevel::Info, QStringLiteral("Removed 2 partial files"));
}

void loadTools(MainWindow &window)
{
    window.tabHeader()->setToolsNotice(QStringLiteral("Installing download tools…"), QStringLiteral("neutral"), QString());
    QueueView *queue = window.queueView();
    queue->setWaitingForTools(true);
    DownloadItem waiting = fixtureItem(1, QStringLiteral("https://www.youtube.com/watch?v=jNQXAC9IVRw"), DownloadStatus::Pending, QString());
    queue->upsertItem(waiting);

    LogView *log = window.logView();
    addLog(log, "09:12:01", LogLevel::Info, QStringLiteral("LGA Video Downloader v" VIDEODOWNLOADER_VERSION " started"));
    addLog(log, "09:12:01", LogLevel::Error, QStringLiteral("\u2717 yt-dlp.exe not found"));
    addLog(log, "09:12:03", LogLevel::Info, QStringLiteral("yt-dlp: downloading 2026.08.19 (17.0 MiB)"));
    addLog(log, "09:12:05", LogLevel::Info, QStringLiteral("Added 1 link to the queue"));
    addLog(log, "09:12:05", LogLevel::Warning, QStringLiteral("Waiting for the download tools to finish installing"));
}

void loadEmpty(MainWindow &window)
{
    LogView *log = window.logView();
    addLog(log, "10:40:02", LogLevel::Info, QStringLiteral("LGA Video Downloader v" VIDEODOWNLOADER_VERSION " started"));
    addLog(log, "10:40:02", LogLevel::Done, QStringLiteral("\u2713 yt-dlp.exe found: C:\\Users\\lega\\AppData\\Local\\LGA\\VideoDownloader\\tools\\yt-dlp.exe"));
    addLog(log, "10:40:02", LogLevel::Done, QStringLiteral("\u2713 ffmpeg.exe found in tools directory"));
}

QJsonObject geometryOf(QWidget *widget, QWidget *root)
{
    const QPoint origin = widget->mapTo(root, QPoint(0, 0));
    return QJsonObject{{QStringLiteral("x"), origin.x()}, {QStringLiteral("y"), origin.y()},
                       {QStringLiteral("w"), widget->width()}, {QStringLiteral("h"), widget->height()}};
}

} // namespace

int runParseCheck(const QStringList &args)
{
    // --qa-parse <texto.txt>: imprime lo que LinkParser saca de un texto pegado, sin red.
    QFile file(args.value(args.indexOf(QStringLiteral("--qa-parse")) + 1));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        fprintf(stderr, "usage: --qa-parse <text.txt>\n");
        return 2;
    }
    const LinkParser::Result result = LinkParser::parse(QString::fromUtf8(file.readAll()));
    for (const QString &link : result.links) {
        fprintf(stdout, "link %s\n", qPrintable(link));
    }
    fprintf(stdout, "ignored %d: %s\n", result.ignoredWords, qPrintable(result.ignoredText));
    return 0;
}

int runMigrateCheck(const QStringList &args)
{
    const int index = args.indexOf(QStringLiteral("--qa-migrate"));
    const QString legacyDir = args.value(index + 1);
    const QString targetDir = args.value(index + 2);
    if (legacyDir.isEmpty() || targetDir.isEmpty()) {
        fprintf(stderr, "qa-migrate: needs <legacy folder> <target folder>\n");
        return 2;
    }
    // La migracion borra la carpeta de origen cuando termina: no se acepta una carpeta que no
    // sea de tools, para que un error de tipeo no barra algo del usuario.
    if (!AppPaths::looksLikeToolsFolder(legacyDir)) {
        fprintf(stderr, "qa-migrate: %s is not a tools folder (needs yt-dlp/deno or tools.json); nothing touched\n",
                qPrintable(QDir::toNativeSeparators(legacyDir)));
        return 2;
    }
    const QStringList lines = AppPaths::migrateToolsFolder(legacyDir, targetDir);
    for (const QString &line : lines) {
        fprintf(stdout, "moved %s\n", qPrintable(line));
    }
    // Estado de las dos carpetas despues de migrar, para comparar en la prueba.
    const auto dump = [](const char *tag, const QString &dir) {
        if (!QDir(dir).exists()) {
            fprintf(stdout, "%s <gone>\n", tag);
            return;
        }
        const QFileInfoList entries = QDir(dir).entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
        for (const QFileInfo &entry : entries) {
            fprintf(stdout, "%s %s %lld\n", tag, qPrintable(entry.fileName()),
                    entry.isDir() ? -1LL : entry.size());
        }
    };
    dump("legacy", legacyDir);
    dump("legacy-staging", legacyDir + QStringLiteral("/.staging"));
    dump("target", targetDir);
    dump("target-staging", targetDir + QStringLiteral("/.staging"));
    return 0;
}

int runToolsCheck(const QStringList &args)
{
    const QString dir = args.value(args.indexOf(QStringLiteral("--qa-tools-check")) + 1);
    if (dir.isEmpty() || !QDir(dir).exists()) {
        fprintf(stderr, "qa-tools-check: needs an existing folder\n");
        return 2;
    }
    // Las mismas funciones que la app consulta antes de lanzar una tool, sobre una carpeta
    // cualquiera. Solo leen cabeceras: este modo no ejecuta ningun archivo de la carpeta.
    const QDir base(dir);
    const QStringList files = base.entryList({QStringLiteral("*.exe"), QStringLiteral("*.dll")}, QDir::Files, QDir::Name);
    for (const QString &name : files) {
        const bool library = name.endsWith(QLatin1String(".dll"), Qt::CaseInsensitive);
        QString reason;
        const bool ok = library ? PeCheck::isValidImage(base.filePath(name), PeCheck::Kind::Library, &reason)
                                : ToolsUpdater::isRunnableBinary(base.filePath(name), &reason);
        fprintf(stdout, "file %s %s%s%s\n", qPrintable(name), ok ? "valid" : "INVALID", ok ? "" : ": ", qPrintable(reason));
    }
    for (ToolsUpdater::Tool tool : {ToolsUpdater::Tool::YtDlp, ToolsUpdater::Tool::Deno}) {
        QString reason;
        const bool ok = ToolsUpdater::isRunnableBinary(base.filePath(ToolsUpdater::binaryName(tool)), &reason);
        fprintf(stdout, "tool %s launch=%d%s%s\n", qPrintable(ToolsUpdater::toolKey(tool)), ok ? 1 : 0, ok ? "" : " ",
                qPrintable(reason));
    }
#ifdef Q_OS_WIN
    const QString ffmpeg = base.filePath(QStringLiteral("ffmpeg.exe"));
#else
    const QString ffmpeg = base.filePath(QStringLiteral("ffmpeg"));
#endif
    const QString problem = QFileInfo(ffmpeg).isFile() ? ToolsManager::ffmpegProblem(ffmpeg)
                                                       : QStringLiteral("the file does not exist");
    fprintf(stdout, "tool ffmpeg launch=%d%s%s\n", problem.isEmpty() ? 1 : 0, problem.isEmpty() ? "" : " ", qPrintable(problem));
    return 0;
}

int runUpdateDirsCheck(const QStringList &args)
{
    const int index = args.indexOf(QStringLiteral("--qa-update-dirs"));
    const QString appDir = args.value(index + 1);
    const QString fallbackDir = args.value(index + 2);
    if (appDir.isEmpty() || fallbackDir.isEmpty()) {
        fprintf(stderr, "qa-update-dirs: needs <app folder> <fallback folder>\n");
        return 2;
    }
    // La misma eleccion que hace la app con su propia carpeta, pero sobre carpetas de prueba.
    bool fallback = false;
    const QString resolved = AppPaths::chooseHeavyDataDir(QDir(appDir).filePath(QStringLiteral("updates")),
                                                          fallbackDir, &fallback);
    fprintf(stdout, "resolved %s fallback=%d\n", qPrintable(QDir::toNativeSeparators(resolved)), fallback ? 1 : 0);
    fprintf(stdout, "temp %s\n", qPrintable(QDir::toNativeSeparators(UpdateService::legacyTempUpdateDir())));

    // El barrido del arranque, sobre las carpetas de prueba y la de %TEMP% (TMP/TEMP del proceso).
    const QStringList dirs = UpdateService::installerSweepDirs(appDir, fallbackDir);
    const int removed = UpdateService::removeOldInstallers(dirs, QString());
    fprintf(stdout, "removed %d\n", removed);
    for (const QString &dir : dirs) {
        if (!QDir(dir).exists()) {
            fprintf(stdout, "left %s <gone>\n", qPrintable(QDir::toNativeSeparators(dir)));
            continue;
        }
        const QStringList entries = QDir(dir).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                                                        QDir::Name);
        fprintf(stdout, "left %s [%s]\n", qPrintable(QDir::toNativeSeparators(dir)),
                qPrintable(entries.join(QStringLiteral(", "))));
    }
    return 0;
}

int runWhatsNewCheck(const QStringList &args)
{
    int failures = 0;
    const auto check = [&failures](const QString &name, bool ok) {
        fprintf(stdout, "%s %s\n", ok ? "PASS" : "FAIL", qPrintable(name));
        failures += ok ? 0 : 1;
    };
    // Regla de version: minor de 3 cifras rellenado a la derecha, ceros finales que no cuentan.
    const struct { const char *a; const char *b; int expected; } versions[] = {
        {"0.96", "0.960", 0}, {"0.95", "0.96", -1}, {"2.66", "2.7", -1}, {"1.015", "1.15", -1},
        {"0.99", "0.991", -1}, {"0.910.0", "0.910", 0}, {"1.2.1", "1.2", 1}, {"v0.96", "0.96", 0},
    };
    for (const auto &v : versions) {
        check(QStringLiteral("compare %1 %2").arg(QLatin1String(v.a), QLatin1String(v.b)),
              WhatsNew::compareVersions(QLatin1String(v.a), QLatin1String(v.b)) == v.expected);
    }
    check(QStringLiteral("version invalida"), !WhatsNew::isValidVersion(QStringLiteral("0.96-beta")));
    using AI = WhatsNew::AfterInstall;
    check(QStringLiteral("after-install: instalacion nueva"), WhatsNew::afterInstallAction(QString(), QStringLiteral("0.96")) == AI::SaveOnly);
    check(QStringLiteral("after-install: valor ilegible"), WhatsNew::afterInstallAction(QStringLiteral("x"), QStringLiteral("0.96")) == AI::SaveOnly);
    check(QStringLiteral("after-install: se actualizo"), WhatsNew::afterInstallAction(QStringLiteral("0.95"), QStringLiteral("0.96")) == AI::Show);
    check(QStringLiteral("after-install: ya vistas"), WhatsNew::afterInstallAction(QStringLiteral("0.96"), QStringLiteral("0.96")) == AI::Nothing);
    check(QStringLiteral("after-install: version mas vieja"), WhatsNew::afterInstallAction(QStringLiteral("0.97"), QStringLiteral("0.96")) == AI::Nothing);

    // Parse estricto y escape del texto.
    const QString product = QStringLiteral("legandrop/LGA_VideoDownloader");
    const QByteArray good = R"({"schemaVersion":1,"product":"legandrop/LGA_VideoDownloader","versions":[
        {"version":"0.95","items":[{"kind":"fixed","text":"old"}]},
        {"version":"0.97","date":"2026-11-01","items":[{"kind":"new","text":"<b>bold</b> & %1","platform":["mac"]},
                                                     {"kind":"improved","text":"both"}]},
        {"version":"0.96","items":[{"kind":"new","text":"win only","platform":["win"]}]}]})";
    const WhatsNew::Notes parsed = WhatsNew::parse(good, product);
    check(QStringLiteral("parse valido, la mas nueva primero"),
          parsed.valid && parsed.versions.size() == 3 && parsed.versions.first().version == QLatin1String("0.97"));
    const auto win = WhatsNew::range(parsed, QStringLiteral("0.95"), QStringLiteral("0.97"), QStringLiteral("win"));
    check(QStringLiteral("rango (0.95, 0.97] en win"), win.size() == 2 && win.at(0).items.size() == 1 && win.at(1).items.size() == 1);
    const auto mac = WhatsNew::range(parsed, QStringLiteral("0.96"), QString(), QStringLiteral("mac"));
    const QString html = WhatsNew::toHtml(mac);
    check(QStringLiteral("rango (0.96, ...] en mac"), mac.size() == 1 && mac.at(0).items.size() == 2);
    check(QStringLiteral("texto escapado"), html.contains(QStringLiteral("&lt;b&gt;bold&lt;/b&gt; &amp; %1"))
                                                && !html.contains(QStringLiteral("<b>bold")));
    const QList<QPair<const char *, QByteArray>> bad = {
        {"otro product", R"({"schemaVersion":1,"product":"x/y","versions":[]})"},
        {"otro schemaVersion", R"({"schemaVersion":2,"product":"legandrop/LGA_VideoDownloader","versions":[]})"},
        {"kind desconocido", R"({"schemaVersion":1,"product":"legandrop/LGA_VideoDownloader","versions":[{"version":"1.0","items":[{"kind":"bug","text":"a"}]}]})"},
        {"texto vacio", R"({"schemaVersion":1,"product":"legandrop/LGA_VideoDownloader","versions":[{"version":"1.0","items":[{"kind":"new","text":" "}]}]})"},
        {"version no numerica", R"({"schemaVersion":1,"product":"legandrop/LGA_VideoDownloader","versions":[{"version":"beta","items":[]}]})"},
        {"platform no es lista", R"({"schemaVersion":1,"product":"legandrop/LGA_VideoDownloader","versions":[{"version":"1.0","items":[{"kind":"new","text":"a","platform":"win"}]}]})"},
        {"JSON roto", R"({"schemaVersion":1,)"},
        {"demasiado grande", QByteArray(WhatsNew::kMaxBytes + 1, ' ')},
    };
    for (const auto &entry : bad) {
        check(QStringLiteral("rechaza %1").arg(QLatin1String(entry.first)), !WhatsNew::parse(entry.second, product).valid);
    }

    // `--qa-whats-new <json> [<instalada> <ofrecida>]`: el rango de un archivo real.
    const int index = args.indexOf(QStringLiteral("--qa-whats-new"));
    const QString path = args.value(index + 1);
    if (!path.isEmpty() && !path.startsWith(QLatin1String("--"))) {
        QFile file(path);
        const WhatsNew::Notes notes = file.open(QIODevice::ReadOnly) ? WhatsNew::parse(file.readAll(), product) : WhatsNew::Notes();
        check(QStringLiteral("archivo %1").arg(path), notes.valid);
        fprintf(stdout, "error '%s' versions %d\n", qPrintable(notes.error), int(notes.versions.size()));
        for (const QString &platform : {QStringLiteral("win"), QStringLiteral("mac")}) {
            for (const WhatsNew::VersionNotes &entry : WhatsNew::range(notes, args.value(index + 2), args.value(index + 3), platform)) {
                fprintf(stdout, "%s v%s %s items %d\n", qPrintable(platform), qPrintable(entry.version),
                        qPrintable(entry.date), int(entry.items.size()));
            }
        }
    }
    fprintf(stdout, "%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}

int runWhatsNewFetch(const QStringList &args)
{
    // Solo contra un servidor local: nunca baja nada de GitHub.
    if (!UpdateUrls::isLocalOverride(QUrl(UpdateUrls::githubBase()))) {
        fprintf(stderr, "qa-whats-new-fetch: set LGA_VD_GITHUB_BASE to a localhost server\n");
        return 2;
    }
    const int index = args.indexOf(QStringLiteral("--qa-whats-new-fetch"));
    const QString cacheDir = args.value(index + 1);
    if (cacheDir.isEmpty() || !QDir(cacheDir).exists()) {
        fprintf(stderr, "usage: --qa-whats-new-fetch <existing-cache-dir> [--qa-close-after-ms N]\n");
        return 2;
    }
    const int closeIndex = args.indexOf(QStringLiteral("--qa-close-after-ms"));
    const int closeAfterMs = closeIndex >= 0 ? args.value(closeIndex + 1).toInt() : -1;
    UpdateService::setNotesCacheDirForQa(cacheDir);

    auto *service = new UpdateService;
    const auto print = [](const QString &line) {
        fprintf(stdout, "%s\n", qPrintable(line));
        fflush(stdout);
    };
    QObject::connect(service, &UpdateService::stateChanged, qApp, [&service, closeAfterMs, print](UpdateService::State state) {
        print(QStringLiteral("state %1").arg(int(state)));
        if (state == UpdateService::State::UpToDate || state == UpdateService::State::CheckFailed) {
            print(QStringLiteral("no update: %1").arg(service->errorString()));
            QCoreApplication::exit(3);
        } else if (state == UpdateService::State::UpdateAvailable && closeAfterMs >= 0) {
            // Cierre con la descarga de notas en curso: el servicio se destruye con su reply viva.
            QTimer::singleShot(closeAfterMs, qApp, [&service, print]() {
                print(QStringLiteral("deleting service, notes status %1").arg(int(service->notesStatus())));
                delete service;
                service = nullptr;
                print(QStringLiteral("service deleted"));
                QTimer::singleShot(500, qApp, []() { QCoreApplication::exit(0); });
            });
        }
    });
    QObject::connect(service, &UpdateService::notesChanged, qApp, [&service, closeAfterMs, cacheDir, print]() {
        const WhatsNew::Notes &notes = service->notes();
        print(QStringLiteral("notes status %1 versions %2").arg(int(service->notesStatus())).arg(notes.versions.size()));
        for (const WhatsNew::VersionNotes &entry : WhatsNew::range(notes, service->currentVersion(),
                                                                   service->availableVersion(), WhatsNew::currentPlatform())) {
            print(QStringLiteral("range v%1 items %2").arg(entry.version).arg(entry.items.size()));
        }
        print(QStringLiteral("cache %1").arg(QFileInfo::exists(QDir(cacheDir).filePath(QLatin1String(WhatsNew::kAssetName)))));
        QCoreApplication::exit(closeAfterMs >= 0 ? 4 : 0);
    });
    QTimer::singleShot(40000, qApp, []() { QCoreApplication::exit(5); });
    service->checkForUpdates();
    const int code = QCoreApplication::exec();
    delete service;
    print(QStringLiteral("exit %1").arg(code));
    return code;
}

int runCookiesCheck()
{
    // Casos fijos, sin red ni archivos: una cookie de cada tipo y las que se deben descartar.
    int failures = 0;
    const auto check = [&failures](const char *name, bool ok) {
        fprintf(stdout, "%s %s\n", ok ? "PASS" : "FAIL", name);
        failures += ok ? 0 : 1;
    };

    const QByteArray message = R"({"v":1,"type":"download","url":"https://www.youtube.com/watch?v=jNQXAC9IVRw",
        "title":"t","browser":"brave","cookies":[
        {"domain":".youtube.com","hostOnly":false,"path":"/","secure":true,"httpOnly":true,"expirationDate":1790000000.75,"name":"SID","value":"abc"},
        {"domain":"www.youtube.com","hostOnly":true,"path":"/watch","secure":false,"httpOnly":false,"name":"PREF","value":"f1=1"},
        {"domain":".example.com","hostOnly":true,"path":"","secure":false,"httpOnly":false,"name":"a","value":"b"},
        {"domain":"vimeo.com","hostOnly":false,"path":"/","secure":true,"httpOnly":false,"expirationDate":-5,"name":"c","value":"d"},
        {"domain":"youtube.com","hostOnly":false,"path":"/","secure":true,"httpOnly":false,"name":"TAB","value":"x\ty"},
        {"domain":"...","hostOnly":false,"path":"/","secure":true,"httpOnly":false,"name":"EMPTY","value":"z"}
        ]})";
    NativeHost::Request request;
    QString code;
    QString text;
    const bool parsed = NativeHost::parseRequest(message, false, &request, &code, &text);
    check("parse download with cookies", parsed && request.type == QLatin1String("download"));
    check("drop cookies with tabs or empty domain", request.cookies.size() == 4 && request.droppedCookies == 2);

    int accepted = 0;
    const QByteArray netscape = SessionCookies::toNetscape(request.cookies, &accepted);
    const QByteArray expected = "# Netscape HTTP Cookie File\n"
                                "# Temporary file written by LGA Video Downloader.\n\n"
                                "#HttpOnly_.youtube.com\tTRUE\t/\tTRUE\t1790000000\tSID\tabc\n"
                                "www.youtube.com\tFALSE\t/watch\tFALSE\t0\tPREF\tf1=1\n"
                                "example.com\tFALSE\t/\tFALSE\t0\ta\tb\n"
                                ".vimeo.com\tTRUE\t/\tTRUE\t0\tc\td\n";
    check("netscape text", netscape == expected && accepted == 4);
    if (netscape != expected) {
        fprintf(stdout, "--- got ---\n%s--- expected ---\n%s", netscape.constData(), expected.constData());
    }

    const auto rejects = [&](const char *name, const QByteArray &json, const char *wantedCode) {
        NativeHost::Request ignored;
        QString gotCode;
        QString gotText;
        const bool ok = NativeHost::parseRequest(json, false, &ignored, &gotCode, &gotText);
        check(name, !ok && gotCode == QLatin1String(wantedCode));
    };
    rejects("reject broken json", R"({"v":1,"type":)", "bad_request");
    rejects("reject newer protocol", R"({"v":2,"type":"ping"})", "unsupported_version");
    rejects("reject file url", R"({"v":1,"type":"download","url":"file:///C:/Windows/win.ini"})", "bad_request");
    rejects("reject javascript url", R"({"v":1,"type":"download","url":"javascript:void0"})", "bad_request");
    rejects("reject cookie with wrong type", R"({"v":1,"type":"download","url":"https://a.com/","cookies":[{"domain":"a.com","path":"/","name":"n","value":1}]})", "bad_request");
    rejects("reject activate from the browser", R"({"v":1,"type":"activate"})", "bad_request");
    rejects("reject long title", QByteArray(R"({"v":1,"type":"download","url":"https://a.com/","title":")")
                                     + QByteArray(513, 'x') + R"("})", "bad_request");
    return failures == 0 ? 0 : 1;
}

int runWalkthrough(const QStringList &args)
{
    // Recorrido real sin escritorio: la app completa (tools, cola, red, settings del usuario)
    // con la plataforma offscreen. Pega los links en la tarjeta real, aprieta el Download
    // real y guarda capturas hasta que la cola termina.
    const int index = args.indexOf(QStringLiteral("--qa-walkthrough"));
    const QString linksPath = args.value(index + 1);
    const QString outDir = args.value(index + 2);
    if (index < 0 || linksPath.isEmpty() || outDir.isEmpty() || !QFileInfo(linksPath).isFile() || !QDir(outDir).exists()) {
        fprintf(stderr, "usage: --qa-walkthrough <links.txt> <existing-out-dir>\n");
        return 2;
    }
    if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
        fprintf(stderr, "qa-walkthrough: run with QT_QPA_PLATFORM=offscreen\n");
        return 2;
    }
    QFile linksFile(linksPath);
    if (!linksFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 2;
    }
    const QString links = QString::fromUtf8(linksFile.readAll());

    // --qa-isolated <carpeta>: modo de prueba de QStandardPaths. La config va a una carpeta
    // "qttest" propia, asi no se toca la del usuario; las tools son las de la carpeta del exe
    // (en Windows es donde viven siempre; en macOS, el seed del bundle). La carpeta indicada es
    // el destino de las descargas.
    const int isolatedIndex = args.indexOf(QStringLiteral("--qa-isolated"));
    if (isolatedIndex >= 0) {
        const QString downloadDir = args.value(isolatedIndex + 1);
        if (downloadDir.isEmpty() || !QDir(downloadDir).exists()) {
            fprintf(stderr, "qa-walkthrough: --qa-isolated needs an existing download folder\n");
            return 2;
        }
        QStandardPaths::setTestModeEnabled(true);
        MainWindow::setAutomaticUpdatesEnabled(false);
        QSettings settings(MainWindow::configPath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("download/folder"), downloadDir);
        // --qa-setting clave=valor (repetible), p.ej. download/quality=best.
        for (int i = 0; i < args.size() - 1; ++i) {
            if (args.at(i) == QLatin1String("--qa-setting")) {
                const QString pair = args.at(i + 1);
                const int eq = pair.indexOf(QLatin1Char('='));
                if (eq > 0) {
                    settings.setValue(pair.left(eq), pair.mid(eq + 1));
                }
            }
        }
        settings.sync();
        fprintf(stdout, "isolated config %s\n", qPrintable(MainWindow::configPath()));
    }

    MainWindow window;
    window.resize(1200, 860);
    window.show();

    int shot = 0;
    const auto save = [&window, &outDir, &shot](const QString &tag) {
        const QString path = QDir(outDir).filePath(QStringLiteral("%1_%2.png").arg(++shot, 3, 10, QLatin1Char('0')).arg(tag));
        window.grab().save(path);
        fprintf(stdout, "shot %s\n", qPrintable(path));
        fflush(stdout);
    };

    bool clicked = false;
    bool cancelled = false;
    const int cancelAtIndex = args.indexOf(QStringLiteral("--qa-cancel-at"));
    const int cancelAt = cancelAtIndex >= 0 ? args.value(cancelAtIndex + 1).toInt() : -1;
    int idleChecks = 0;
    QElapsedTimer clock;
    clock.start();
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &window, [&]() {
        DownloadQueue *queue = window.downloadQueue();
        const ToolsManager *tools = window.toolsManager();
        if (!clicked) {
            // Espera a que la ventana termine de arrancar (tools chequeadas) antes de pegar.
            if (clock.elapsed() < 3000 || !tools || tools->status() == ToolsManager::Status::Checking) {
                return;
            }
            save(QStringLiteral("start"));
            window.addCard()->setLinksText(links);
            save(QStringLiteral("pasted"));
            for (QPushButton *button : window.addCard()->findChildren<QPushButton *>()) {
                if (button->text() == QLatin1String("Download")) {
                    button->click();
                    clicked = true;
                }
            }
            save(QStringLiteral("clicked"));
            return;
        }
        save(QStringLiteral("progress"));
        // --qa-cancel-at N: al pasar N% en el item que descarga, aprieta su boton Cancel real.
        if (cancelAt >= 0 && !cancelled) {
            for (const DownloadItem &item : queue->items()) {
                if (item.status == DownloadStatus::Downloading && item.progress >= cancelAt) {
                    for (QPushButton *button : window.queueView()->findChildren<QPushButton *>()) {
                        if (button->toolTip() == QLatin1String("Cancel") && button->isVisibleTo(&window)) {
                            fprintf(stdout, "cancel at %d%%\n", item.progress);
                            button->click();
                            cancelled = true;
                            break;
                        }
                    }
                }
            }
            if (cancelled) {
                save(QStringLiteral("cancelled"));
                return;
            }
        }
        const bool idle = queue && queue->activeDownloadCount() == 0;
        idleChecks = idle ? idleChecks + 1 : 0;
        if (idleChecks >= 2 || clock.elapsed() > 10 * 60 * 1000) {
            save(idle ? QStringLiteral("final") : QStringLiteral("timeout"));
            QJsonArray items;
            for (const DownloadItem &item : queue->items()) {
                items.append(QJsonObject{{QStringLiteral("url"), item.url}, {QStringLiteral("status"), int(item.status)},
                                         {QStringLiteral("title"), item.title}, {QStringLiteral("file"), item.filePath},
                                         {QStringLiteral("headline"), item.errorHeadline},
                                         {QStringLiteral("detail"), item.errorDetail},
                                         {QStringLiteral("bytes"), item.totalBytes}});
            }
            QFile out(QDir(outDir).filePath(QStringLiteral("items.json")));
            if (out.open(QIODevice::WriteOnly)) {
                out.write(QJsonDocument(items).toJson());
            }
            QFile log(QDir(outDir).filePath(QStringLiteral("log.txt")));
            if (log.open(QIODevice::WriteOnly)) {
                log.write(window.logView()->canvas()->plainText().toUtf8());
            }
            QCoreApplication::exit(idle ? 0 : 3);
        }
    });
    poll.start(2500);
    return QCoreApplication::exec();
}

int runUiShot(const QStringList &args)
{
    const int index = args.indexOf(QStringLiteral("--ui-shot"));
    if (index < 0 || index + 2 >= args.size()) {
        fprintf(stderr, "usage: --ui-shot <%s> <out.png> [--dpr <1..3>] [--size WxH]\n", qPrintable(kStates.join('|')));
        return 2;
    }
    const QString state = args.at(index + 1);
    const QString outPath = QFileInfo(args.at(index + 2)).absoluteFilePath();
    qreal dpr = 1.0;
    const int dprIndex = args.indexOf(QStringLiteral("--dpr"));
    if (dprIndex >= 0) {
        bool ok = false;
        dpr = args.value(dprIndex + 1).toDouble(&ok);
        if (!ok || dpr < 1.0 || dpr > 3.0) {
            fprintf(stderr, "ui-shot: invalid --dpr\n");
            return 2;
        }
    }
    int shotWidth = SHOT_WIDTH;
    int shotHeight = SHOT_HEIGHT;
    const int sizeIndex = args.indexOf(QStringLiteral("--size"));
    if (sizeIndex >= 0) {
        const QStringList parts = args.value(sizeIndex + 1).split(QLatin1Char('x'));
        bool okW = false, okH = false;
        shotWidth = parts.value(0).toInt(&okW);
        shotHeight = parts.value(1).toInt(&okH);
        if (parts.size() != 2 || !okW || !okH || shotWidth < 900 || shotHeight < 640 || shotWidth > 4000 || shotHeight > 3000) {
            fprintf(stderr, "ui-shot: invalid --size (WxH, minimum 900x640)\n");
            return 2;
        }
    }
    if (!kStates.contains(state)) {
        fprintf(stderr, "ui-shot: unknown state '%s'\n", qPrintable(state));
        return 2;
    }
    if (!outPath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive) || QFileInfo::exists(outPath)
        || !QFileInfo(outPath).dir().exists()) {
        fprintf(stderr, "ui-shot: output must be a new .png in an existing folder\n");
        return 2;
    }
    const bool withNotes = state == QLatin1String("help-update-notes") || state == QLatin1String("help-history")
                           || state == QLatin1String("after-install");
    const WhatsNew::Notes notes = fixtureNotes(args);
    if (withNotes && !notes.valid) {
        fprintf(stderr, "ui-shot: %s needs --notes <valid whats_new.json> (%s)\n", qPrintable(state), qPrintable(notes.error));
        return 2;
    }
    // Las notas de prueba van de la version que corre a la siguiente que traiga el archivo.
    const QString shotCurrent = QStringLiteral(VIDEODOWNLOADER_VERSION);
    QString shotOffered = shotCurrent;
    for (const WhatsNew::VersionNotes &entry : notes.versions) {
        if (WhatsNew::compareVersions(entry.version, shotOffered) > 0) {
            shotOffered = entry.version;
        }
    }
    const QString rangeHtml = WhatsNew::toHtml(WhatsNew::range(notes, shotCurrent, shotOffered, WhatsNew::currentPlatform()));

    MainWindow window(MainWindow::Mode::Capture);
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    // Como el sistema de ventanas: nunca por debajo del minimo que piden los layouts. Si el
    // tamano pedido era menor se captura el minimo y se informa.
    const QSize requested(shotWidth, shotHeight);
    shotWidth = qMax(shotWidth, window.minimumWidth());
    shotHeight = qMax(shotHeight, window.layout()->minimumSize().height());
    if (QSize(shotWidth, shotHeight) != requested) {
        fprintf(stdout, "ui-shot clamped %dx%d -> %dx%d\n", requested.width(), requested.height(), shotWidth, shotHeight);
    }
    window.resize(shotWidth, shotHeight);

    AddVideosCard *card = window.addCard();
    card->setBrowsers(fixtureBrowsers());
    card->setDownloadFolder(QStringLiteral("D:/Downloads/Video"));
    window.queueView()->setFirefoxAvailable(true);

    const bool help = state.startsWith(QLatin1String("help"));
    if (state == QLatin1String("empty")) {
        card->setCookiesSource(QStringLiteral("firefox"), QString());
        loadEmpty(window);
    } else if (state == QLatin1String("downloading") || help) {
        card->setCookiesSource(QStringLiteral("firefox"), QString());
        loadDownloading(window);
    } else if (state == QLatin1String("error")) {
        card->setCookiesSource(QString(), QString());
        card->setCookiesAttention(true);
        loadError(window);
    } else if (state == QLatin1String("other-errors")) {
        card->setCookiesSource(QStringLiteral("firefox"), QString());
        loadOtherErrors(window);
    } else if (state == QLatin1String("tools")) {
        card->setCookiesSource(QStringLiteral("firefox"), QString());
        loadTools(window);
    }

    QDialog *dialog = nullptr;
    if (state == QLatin1String("after-install")) {
        card->setCookiesSource(QStringLiteral("firefox"), QString());
        loadEmpty(window);
        auto *scrim = new Scrim(window.centralWidget());
        scrim->setVisible(true);
        auto *afterInstall = new WhatsNewDialog(shotOffered, rangeHtml, window.centralWidget());
        afterInstall->setWindowFlags(Qt::Widget);
        afterInstall->fitHeight();
        afterInstall->setVisible(true);
        dialog = afterInstall;
    }
    if (help) {
        window.tabHeader()->setUpdateNotice(state == QLatin1String("help") || state == QLatin1String("help-history") ? QString()
                                            : state == QLatin1String("help-update") ? QStringLiteral("Update available · v0.90")
                                            : state == QLatin1String("help-update-notes") ? QStringLiteral("Update available · v%1").arg(shotOffered)
                                                                                          : QStringLiteral("Downloading update…"));
        auto *scrim = new Scrim(window.centralWidget());
        scrim->setVisible(true);
        auto *helpDialog = new HelpDialog(window.centralWidget());
        dialog = helpDialog;
        // Hijo comun dentro de la ventana, no una ventana propia: se dibuja con el mismo render.
        helpDialog->setWindowFlags(Qt::Widget);
        helpDialog->setToolVersions({{QStringLiteral("yt-dlp"), QStringLiteral("2026.08.19")},
                                     {QStringLiteral("ffmpeg"), QStringLiteral("N-117208-gbd22d7e601-20240927")},
                                     {QStringLiteral("deno"), QStringLiteral("2.9.6")}});
        UpdateView view;
        view.currentVersion = QStringLiteral(VIDEODOWNLOADER_VERSION);
        view.lastChecked = QDateTime(QDate::currentDate(), QTime(10, 40));
        if (state == QLatin1String("help") || state == QLatin1String("help-history")) {
            view.state = UpdateService::State::UpToDate;
            if (state == QLatin1String("help-history")) {
                helpDialog->setHistory(WhatsNew::toHtml(WhatsNew::range(notes, QString(), QString(), WhatsNew::currentPlatform())));
                helpDialog->setHistoryVisible(true);
            }
        } else if (state == QLatin1String("help-update-notes")) {
            view.state = UpdateService::State::UpdateAvailable;
            view.availableVersion = shotOffered;
            view.notesHtml = rangeHtml;
        } else if (state == QLatin1String("help-update")) {
            view.state = UpdateService::State::UpdateAvailable;
            view.availableVersion = QStringLiteral("0.90");
        } else {
            view.state = UpdateService::State::Downloading;
            view.availableVersion = QStringLiteral("0.90");
            view.received = 18LL * 1024 * 1024;
            view.total = 42LL * 1024 * 1024;
        }
        helpDialog->setUpdateView(view);
        helpDialog->setVisible(true);
    }

    // Resolver layouts: render() activa los layouts de widgets nunca mostrados, pero los
    // eventos de layout pendientes se procesan antes para no capturar un estado intermedio.
    for (int pass = 0; pass < 3; ++pass) {
        QCoreApplication::sendPostedEvents();
        QPixmap warmup(1, 1);
        window.render(&warmup);
        if (dialog) {
            dialog->adjustSize();
            dialog->move((shotWidth - dialog->width()) / 2, (shotHeight - dialog->height()) / 2);
        }
    }
    QCoreApplication::sendPostedEvents();

    QPixmap pixmap(QSize(shotWidth, shotHeight) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(QColor(QLatin1String("#161616")));
    window.render(&pixmap, QPoint(), QRegion(), QWidget::DrawWindowBackground | QWidget::DrawChildren);

    QImage image = pixmap.toImage().convertToFormat(QImage::Format_RGB32);
    if (!image.save(outPath, "PNG")) {
        fprintf(stderr, "ui-shot: could not save %s\n", qPrintable(outPath));
        return 1;
    }
    const QImage check(outPath);
    if (check.size() != QSize(shotWidth, shotHeight) * dpr) {
        fprintf(stderr, "ui-shot: saved image has unexpected size\n");
        return 1;
    }

    // Descriptor para verificar la captura: geometria de las piezas y fuentes resueltas.
    QJsonObject descriptor;
    descriptor.insert(QStringLiteral("state"), state);
    descriptor.insert(QStringLiteral("dpr"), dpr);
    descriptor.insert(QStringLiteral("logical"), QJsonArray{shotWidth, shotHeight});
    descriptor.insert(QStringLiteral("requested"), QJsonArray{requested.width(), requested.height()});
    descriptor.insert(QStringLiteral("physical"), QJsonArray{check.width(), check.height()});
    descriptor.insert(QStringLiteral("pid"), qint64(QCoreApplication::applicationPid()));
    descriptor.insert(QStringLiteral("version"), QStringLiteral(VIDEODOWNLOADER_VERSION));
    descriptor.insert(QStringLiteral("fixture"), true);
    QJsonObject geometry;
    geometry.insert(QStringLiteral("tabHeader"), geometryOf(window.tabHeader(), &window));
    geometry.insert(QStringLiteral("addCard"), geometryOf(window.addCard(), &window));
    geometry.insert(QStringLiteral("queueView"), geometryOf(window.queueView(), &window));
    geometry.insert(QStringLiteral("logView"), geometryOf(window.logView(), &window));
    QJsonArray tiles;
    for (QWidget *tile : window.queueView()->findChildren<QWidget *>(QStringLiteral("tile"))) {
        if (tile->isVisibleTo(&window)) {
            tiles.append(geometryOf(tile, &window));
        }
    }
    geometry.insert(QStringLiteral("tiles"), tiles);
    if (dialog) {
        geometry.insert(dialog->objectName(), geometryOf(dialog, &window));
    }
    descriptor.insert(QStringLiteral("geometry"), geometry);
    // Arbol completo de widgets visibles (clase, nombre, rectangulo en coordenadas de la
    // ventana) para medir espaciados sin adivinar desde los pixeles.
    QJsonArray tree;
    for (QWidget *widget : window.findChildren<QWidget *>()) {
        if (!widget->isVisibleTo(&window)) {
            continue;
        }
        QJsonObject entry = geometryOf(widget, &window);
        entry.insert(QStringLiteral("class"), QString::fromLatin1(widget->metaObject()->className()));
        entry.insert(QStringLiteral("name"), widget->objectName());
        if (auto *labelWidget = qobject_cast<QLabel *>(widget)) {
            entry.insert(QStringLiteral("text"), labelWidget->text().left(60));
            // Ancho que pide el texto: si el rectangulo es mas angosto, el label esta recortado.
            entry.insert(QStringLiteral("hintWidth"), labelWidget->sizeHint().width());
            if (!labelWidget->toolTip().isEmpty()) {
                entry.insert(QStringLiteral("toolTip"), labelWidget->toolTip());
            }
        } else if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
            entry.insert(QStringLiteral("text"), button->text());
        }
        tree.append(entry);
    }
    descriptor.insert(QStringLiteral("widgets"), tree);
    QJsonObject fonts;
    const auto fontOf = [](const QFont &font) {
        const QFontInfo info(font);
        return QJsonObject{{QStringLiteral("family"), info.family()}, {QStringLiteral("pixelSize"), info.pixelSize()},
                           {QStringLiteral("weight"), info.weight()}, {QStringLiteral("exactMatch"), info.exactMatch()}};
    };
    if (auto *title = window.addCard()->findChild<QLabel *>(QStringLiteral("cardTitle"))) {
        fonts.insert(QStringLiteral("cardTitle"), fontOf(title->font()));
    }
    fonts.insert(QStringLiteral("log"), fontOf(window.logView()->canvas()->font()));
    fonts.insert(QStringLiteral("app"), fontOf(QApplication::font()));
    descriptor.insert(QStringLiteral("fonts"), fonts);

    QSaveFile json(outPath + QStringLiteral(".json"));
    if (json.open(QIODevice::WriteOnly)) {
        json.write(QJsonDocument(descriptor).toJson());
        json.commit();
    }
    fprintf(stdout, "ui-shot ok %s\n", qPrintable(outPath));
    return 0;
}

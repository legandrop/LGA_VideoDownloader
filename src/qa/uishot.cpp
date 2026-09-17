#include "videodownloader/uishot.h"
#include "videodownloader/addvideoscard.h"
#include "videodownloader/helpdialog.h"
#include "videodownloader/logview.h"
#include "videodownloader/mainwindow.h"
#include "videodownloader/queueview.h"
#include "videodownloader/tabheader.h"

#include "videodownloader/downloadqueue.h"
#include "videodownloader/linkparser.h"
#include "videodownloader/nativehost.h"
#include "videodownloader/sessioncookies.h"
#include "videodownloader/toolsmanager.h"

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
    QStringLiteral("other-errors"),
};

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

    HelpDialog *dialog = nullptr;
    if (help) {
        window.tabHeader()->setUpdateNotice(state == QLatin1String("help") ? QString() : state == QLatin1String("help-update") ? QStringLiteral("Update available · v0.90") : QStringLiteral("Downloading update…"));
        auto *scrim = new Scrim(window.centralWidget());
        scrim->setVisible(true);
        dialog = new HelpDialog(window.centralWidget());
        // Hijo comun dentro de la ventana, no una ventana propia: se dibuja con el mismo render.
        dialog->setWindowFlags(Qt::Widget);
        dialog->setToolVersions({{QStringLiteral("yt-dlp"), QStringLiteral("2026.08.19")},
                                 {QStringLiteral("ffmpeg"), QStringLiteral("7.1-full_build")},
                                 {QStringLiteral("deno"), QStringLiteral("2.9.6")}});
        UpdateView view;
        view.currentVersion = QStringLiteral(VIDEODOWNLOADER_VERSION);
        view.lastChecked = QDateTime(QDate::currentDate(), QTime(10, 40));
        if (state == QLatin1String("help")) {
            view.state = UpdateService::State::UpToDate;
        } else if (state == QLatin1String("help-update")) {
            view.state = UpdateService::State::UpdateAvailable;
            view.availableVersion = QStringLiteral("0.90");
        } else {
            view.state = UpdateService::State::Downloading;
            view.availableVersion = QStringLiteral("0.90");
            view.received = 18LL * 1024 * 1024;
            view.total = 42LL * 1024 * 1024;
        }
        dialog->setUpdateView(view);
        dialog->setVisible(true);
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
        geometry.insert(QStringLiteral("helpDialog"), geometryOf(dialog, &window));
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

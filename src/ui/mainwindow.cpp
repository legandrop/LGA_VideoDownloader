#include "videodownloader/mainwindow.h"
#include "videodownloader/addvideoscard.h"
#include "videodownloader/downloadqueue.h"
#include "videodownloader/hostregistration.h"
#include "videodownloader/linkparser.h"
#include "videodownloader/sessioncookies.h"
#include "videodownloader/logview.h"
#include "videodownloader/queueview.h"
#include "videodownloader/tabheader.h"
#include "videodownloader/toolsmanager.h"
#include "videodownloader/updateservice.h"
#include "videodownloader/videopassworddialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Retraso del auto-update (tools y app) despues de construir la ventana: deja que la UI
// termine de aparecer antes de meter trafico de red y procesos.
constexpr int AUTO_UPDATE_DELAY_MS = 2000;

// Ancho del diseno aprobado. El alto es mayor que el del artboard (748 sin la barra del
// sistema): con las tarjetas de cola, a 748 solo entran dos items y medio; a 860 entran cuatro.
constexpr int DEFAULT_WIDTH = 1200;
constexpr int DEFAULT_HEIGHT = 860;
constexpr int MIN_WIDTH = 900;
// Lo que ocupa la barra de titulo del sistema por encima del area cliente.
constexpr int TITLE_BAR_ALLOWANCE = 40;

bool g_automaticUpdates = true;

} // namespace

void MainWindow::setAutomaticUpdatesEnabled(bool enabled)
{
    g_automaticUpdates = enabled;
}

MainWindow::MainWindow(Mode mode, QWidget *parent)
    : QMainWindow(parent)
    , m_mode(mode)
{
    if (m_mode == Mode::Capture) {
        // Settings propios y descartables: la captura nunca lee ni escribe los del usuario.
        m_captureSettingsPath = QDir::temp().filePath(
            QStringLiteral("lga_videodownloader_uishot_%1.ini").arg(QCoreApplication::applicationPid()));
        QFile::remove(m_captureSettingsPath);
        m_settings = new QSettings(m_captureSettingsPath, QSettings::IniFormat, this);
    } else {
        m_settings = new QSettings(configPath(), QSettings::IniFormat, this);
    }

    // La version sale de la macro del CMakeLists, nunca de un literal.
    setWindowTitle(QStringLiteral("LGA Video Downloader v" VIDEODOWNLOADER_VERSION));
    setupUi();

    if (m_mode == Mode::Normal) {
        loadSettings();
        m_browsers = BrowserDetect::detectInstalled();
        m_addCard->setBrowsers(m_browsers);
        m_addCard->setCookiesSource(m_settings->value(QStringLiteral("auth/cookiesBrowser")).toString(),
                                    m_settings->value(QStringLiteral("auth/cookiesFile")).toString());
        // Si lo guardado ya no se ofrece (Chrome en Windows, un navegador desinstalado), se
        // normaliza a None para no pasarle a yt-dlp algo distinto de lo que muestra la UI.
        if (m_addCard->cookiesSource() != m_settings->value(QStringLiteral("auth/cookiesBrowser")).toString()) {
            m_settings->setValue(QStringLiteral("auth/cookiesBrowser"), m_addCard->cookiesSource());
        }
        bool firefox = false;
        for (const BrowserDetect::Browser &browser : std::as_const(m_browsers)) {
            firefox = firefox || (browser.key == QLatin1String("firefox") && browser.supported);
        }
        m_queueView->setFirefoxAvailable(firefox);
        log(QStringLiteral("LGA Video Downloader v" VIDEODOWNLOADER_VERSION " started"));
        setupServices();
    }

    const QRect available = QApplication::primaryScreen() ? QApplication::primaryScreen()->availableGeometry()
                                                          : QRect(0, 0, DEFAULT_WIDTH, DEFAULT_HEIGHT);
    // Alto minimo: el que piden los layouts (Add videos entera, una tarjeta de error completa
    // en la cola y 4 lineas de log), no una constante que pueda quedar corta.
    setMinimumWidth(MIN_WIDTH);
    const int minimumHeight = qMax(layout() ? layout()->minimumSize().height() : 0, minimumSizeHint().height());
    // Nunca mas alto que el area de trabajo (menos la barra de titulo del sistema).
    const int height = qMax(minimumHeight, qMin(DEFAULT_HEIGHT, available.height() - TITLE_BAR_ALLOWANCE));
    resize(qMax(MIN_WIDTH, qMin(DEFAULT_WIDTH, available.width() - 40)), height);
    if (m_mode == Mode::Normal) {
        move(available.left() + (available.width() - width()) / 2,
             available.top() + qMax(0, (available.height() - TITLE_BAR_ALLOWANCE - height) / 2));
    }
}

MainWindow::~MainWindow()
{
    if (!m_captureSettingsPath.isEmpty()) {
        delete m_settings;
        m_settings = nullptr;
        QFile::remove(m_captureSettingsPath);
    }
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralWidget"));
    setCentralWidget(central);

    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tabHeader = new TabHeader(central);
    layout->addWidget(m_tabHeader);

    auto *content = new QWidget(central);
    content->setObjectName(QStringLiteral("content"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(16, 14, 16, 16);
    contentLayout->setSpacing(12);

    m_addCard = new AddVideosCard(content);
    m_queueView = new QueueView(content);
    m_logView = new LogView(content);
    contentLayout->addWidget(m_addCard);
    contentLayout->addWidget(m_queueView, 1);
    contentLayout->addWidget(m_logView);
    layout->addWidget(content, 1);

    connect(m_tabHeader, &TabHeader::helpClicked, this, &MainWindow::openHelp);
    connect(m_tabHeader, &TabHeader::updateNoticeClicked, this, &MainWindow::openHelp);
    connect(m_tabHeader, &TabHeader::toolsNoticeClicked, this, [this]() {
        if (m_toolsManager && m_toolsManager->status() == ToolsManager::Status::Missing) {
            m_toolsManager->retryInstall();
        }
    });

    connect(m_addCard, &AddVideosCard::downloadRequested, this, &MainWindow::onDownloadRequested);
    connect(m_addCard, &AddVideosCard::browseRequested, this, &MainWindow::onBrowseRequested);
    connect(m_addCard, &AddVideosCard::cookiesSourceActivated, this, &MainWindow::onCookiesSourceActivated);
    connect(m_addCard, &AddVideosCard::formatChanged, this, [this](OutputFormat format) {
        m_settings->setValue(QStringLiteral("download/format"),
                             format == OutputFormat::AudioM4a ? QStringLiteral("m4a") : QStringLiteral("mp4"));
    });
    connect(m_addCard, &AddVideosCard::qualityChanged, this, [this](VideoQuality quality) {
        m_settings->setValue(QStringLiteral("download/quality"),
                             quality == VideoQuality::Best ? QStringLiteral("best") : QStringLiteral("compatible"));
    });
}

void MainWindow::setupServices()
{
    m_toolsManager = new ToolsManager(this);
    connect(m_toolsManager, &ToolsManager::logLine, this, [this](const QString &line) {
        log(line, classifyLogLine(line));
    });
    connect(m_toolsManager, &ToolsManager::statusChanged, this, &MainWindow::onToolsStatusChanged);

    m_downloadQueue = new DownloadQueue(m_toolsManager, this);
    connect(m_downloadQueue, &DownloadQueue::itemAdded, m_queueView, &QueueView::upsertItem);
    connect(m_downloadQueue, &DownloadQueue::itemUpdated, m_queueView, &QueueView::upsertItem);
    connect(m_downloadQueue, &DownloadQueue::itemRemoved, m_queueView, &QueueView::removeItem);
    connect(m_downloadQueue, &DownloadQueue::itemUpdated, this, &MainWindow::refreshCookiesAttention);
    connect(m_downloadQueue, &DownloadQueue::itemRemoved, this, &MainWindow::refreshCookiesAttention);
    connect(m_downloadQueue, &DownloadQueue::logLine, this, &MainWindow::log);
    connect(m_downloadQueue, &DownloadQueue::videoPasswordRequired, this, &MainWindow::onVideoPasswordRequired);

    connect(m_queueView, &QueueView::cancelRequested, m_downloadQueue, &DownloadQueue::cancelItem);
    connect(m_queueView, &QueueView::removeRequested, m_downloadQueue, &DownloadQueue::removeItem);
    connect(m_queueView, &QueueView::retryRequested, this, &MainWindow::onRetryRequested);
    connect(m_queueView, &QueueView::retryWithBrowserRequested, this, &MainWindow::onRetryWithBrowser);
    connect(m_queueView, &QueueView::showRequested, this, &MainWindow::onShowRequested);
    connect(m_queueView, &QueueView::copyErrorRequested, this, &MainWindow::onCopyErrorRequested);
    connect(m_queueView, &QueueView::clearFinishedRequested, m_downloadQueue, &DownloadQueue::clearFinished);
    connect(m_queueView, &QueueView::cancelAllRequested, m_downloadQueue, &DownloadQueue::cancelAll);
    connect(m_queueView, &QueueView::retryFailedRequested, this, [this]() {
        // Igual que el Retry de cada tarjeta: formato, calidad y carpeta de cada item, la
        // sesion elegida ahora.
        for (const DownloadItem &item : m_downloadQueue->items()) {
            if (item.status == DownloadStatus::Failed && item.isRetryable()) {
                onRetryRequested(item.id);
            }
        }
    });

    // El swap de tools espera a que no haya un yt-dlp corriendo.
    m_toolsManager->setProcessActiveProbe([this]() {
        return m_downloadQueue && m_downloadQueue->hasActiveProcess();
    });
    m_toolsManager->checkToolsInstallation();

    // Update de la app: el hook corta la cola y mata yt-dlp con sus hijos antes del instalador.
    m_updateService = new UpdateService(this);
    m_updateService->setBeforeInstallHook([this]() {
        if (m_downloadQueue) {
            m_downloadQueue->stopAllForShutdown();
        }
    });
    connect(m_updateService, &UpdateService::stateChanged, this, [this](UpdateService::State state) {
        if (state == UpdateService::State::UpToDate || state == UpdateService::State::UpdateAvailable) {
            m_lastUpdateCheck = QDateTime::currentDateTime();
        }
        if (state == UpdateService::State::UpdateAvailable) {
            log(QStringLiteral("Update available: v%1").arg(m_updateService->availableVersion()));
        } else if (state == UpdateService::State::InstallFailed || state == UpdateService::State::CheckFailed) {
            log(QStringLiteral("Update: %1").arg(m_updateService->errorString()), LogLevel::Warning);
        }
        refreshUpdateNotice();
    });
    connect(m_updateService, &UpdateService::installProgress, this, [this](qint64 received, qint64 total) {
        m_installReceived = received;
        m_installTotal = total;
        if (m_helpDialog) {
            m_helpDialog->setUpdateView(currentUpdateView());
        }
    });
    connect(m_toolsManager, &ToolsManager::toolVersionsChanged, this, [this]() {
        if (m_helpDialog) {
            m_helpDialog->setToolVersions(m_toolsManager->toolVersions());
        }
    });

    // yt-dlp y deno se instalan/actualizan solos y en silencio; la app solo chequea.
    QTimer::singleShot(AUTO_UPDATE_DELAY_MS, this, [this]() {
        m_toolsManager->refreshToolVersions();
        if (!g_automaticUpdates) {
            log(QStringLiteral("Automatic updates are off for this run"), LogLevel::Warning);
            return;
        }
        m_toolsManager->startAutomaticUpdate();
        m_updateService->checkForUpdates();
    });
    onToolsStatusChanged();
}

void MainWindow::log(const QString &text, LogLevel level)
{
    m_logView->append(text, level);
}

void MainWindow::loadSettings()
{
    QString folder = m_settings->value(QStringLiteral("download/folder")).toString();
    if (folder.isEmpty() || !isValidDownloadPath(folder)) {
        // Sin carpeta valida se propone Descargas: el usuario no tiene que configurar nada
        // antes de su primer link.
        folder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (folder.isEmpty()) {
            folder = QDir::homePath();
        }
        m_settings->setValue(QStringLiteral("download/folder"), folder);
    }
    m_addCard->setDownloadFolder(folder);
    m_addCard->setFormat(m_settings->value(QStringLiteral("download/format")).toString() == QLatin1String("m4a")
                             ? OutputFormat::AudioM4a : OutputFormat::VideoMp4);
    m_addCard->setQuality(m_settings->value(QStringLiteral("download/quality")).toString() == QLatin1String("best")
                              ? VideoQuality::Best : VideoQuality::Compatible);
}

DownloadOptions MainWindow::currentOptions() const
{
    DownloadOptions options;
    options.downloadDir = m_settings->value(QStringLiteral("download/folder")).toString();
    options.format = m_addCard->format();
    options.quality = m_addCard->quality();
    const QString source = m_addCard->cookiesSource();
    if (source == QLatin1String("file")) {
        options.cookiesFile = m_settings->value(QStringLiteral("auth/cookiesFile")).toString();
    } else {
        options.cookiesBrowser = source;
    }
    return options;
}

void MainWindow::onDownloadRequested()
{
    if (!m_downloadQueue) {
        return;
    }
    const QString pasted = m_addCard->linksText();
    if (pasted.trimmed().isEmpty()) {
        log(QStringLiteral("Paste at least one video link, then press Download"), LogLevel::Warning);
        return;
    }
    const LinkParser::Result links = LinkParser::parse(pasted);

    DownloadOptions options = currentOptions();
    if (!isValidDownloadPath(options.downloadDir)) {
        QMessageBox::warning(this, QStringLiteral("Download folder"),
                             QStringLiteral("The download folder does not exist. Choose another one with Browse."));
        return;
    }
    if (m_addCard->cookiesSource() == QLatin1String("file")
        && (options.cookiesFile.isEmpty() || !QFileInfo::exists(options.cookiesFile))) {
        QMessageBox::warning(this, QStringLiteral("cookies.txt"),
                             QStringLiteral("The selected cookies.txt file no longer exists. Choose it again in Use cookies from."));
        return;
    }

    // Solo lo que parece un link: texto suelto no crea una tarjeta por palabra.
    // Cualquier sitio: si yt-dlp no lo soporta, la tarjeta lo dice.
    for (const QString &link : links.links) {
        m_downloadQueue->addDownload(link, options);
    }
    const int added = links.links.size();
    if (added > 0) {
        log(added == 1 ? QStringLiteral("Added 1 link to the queue") : QStringLiteral("Added %1 links to the queue").arg(added));
    }
    if (links.ignoredWords > 0) {
        if (added == 0) {
            m_downloadQueue->addNoLinkFound(links.ignoredText);
        } else {
            log(QStringLiteral("%1 %2 ignored (not links): %3")
                    .arg(links.ignoredWords)
                    .arg(links.ignoredWords == 1 ? QStringLiteral("item") : QStringLiteral("items"))
                    .arg(links.ignoredText.left(120)),
                LogLevel::Warning);
        }
    }
    m_addCard->clearLinks();
}

void MainWindow::onBrowseRequested()
{
    const QString current = m_settings->value(QStringLiteral("download/folder")).toString();
    const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Select download folder"), current);
    if (folder.isEmpty()) {
        return;
    }
    m_settings->setValue(QStringLiteral("download/folder"), folder);
    m_addCard->setDownloadFolder(folder);
    log(QStringLiteral("Save to: %1").arg(QDir::toNativeSeparators(folder)));
}

void MainWindow::onCookiesSourceActivated(const QString &key)
{
    QString cookiesFile = m_settings->value(QStringLiteral("auth/cookiesFile")).toString();
    if (key == QLatin1String("file")) {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Select cookies.txt (Netscape format)"),
                                                          QFileInfo(cookiesFile).absolutePath(),
                                                          QStringLiteral("Cookies files (*.txt);;All files (*)"));
        if (file.isEmpty()) {
            // Cancelado: vuelve a lo que estaba guardado.
            m_addCard->setCookiesSource(m_settings->value(QStringLiteral("auth/cookiesBrowser")).toString(), cookiesFile);
            return;
        }
        cookiesFile = file;
        m_settings->setValue(QStringLiteral("auth/cookiesFile"), file);
        log(QStringLiteral("Cookies: using the file %1").arg(QDir::toNativeSeparators(file)));
    } else if (key.isEmpty()) {
        log(QStringLiteral("Cookies: none (public videos only)"));
    } else {
        log(QStringLiteral("Cookies: using the %1 session").arg(BrowserDetect::displayName(key)));
    }
    m_settings->setValue(QStringLiteral("auth/cookiesBrowser"), key);
    m_settings->sync();
    m_addCard->setCookiesSource(key, cookiesFile);
    refreshCookiesAttention();
}

void MainWindow::refreshCookiesAttention()
{
    if (!m_downloadQueue) {
        return;
    }
    // En rojo mientras haya un fallo de sesion con la misma eleccion que muestra el combo:
    // al cambiarla (el arreglo) el resaltado se apaga.
    const DownloadOptions now = currentOptions();
    bool attention = false;
    for (const DownloadItem &item : m_downloadQueue->items()) {
        // Los items con la sesion de la extension no cuentan: el combo no es su fuente.
        const bool sessionProblem = item.status == DownloadStatus::Failed && !item.options.hasSession()
            && (item.failure == FailureKind::NeedsSignIn || item.failure == FailureKind::CookiesUnreadable);
        if (sessionProblem && item.options.cookiesBrowser == now.cookiesBrowser && item.options.cookiesFile == now.cookiesFile) {
            attention = true;
            break;
        }
    }
    m_addCard->setCookiesAttention(attention);
}

void MainWindow::onToolsStatusChanged()
{
    if (!m_toolsManager) {
        return;
    }
    switch (m_toolsManager->status()) {
    case ToolsManager::Status::Checking:
    case ToolsManager::Status::Ready:
        m_tabHeader->setToolsNotice(QString(), QString(), QString());
        break;
    case ToolsManager::Status::Installing:
        m_tabHeader->setToolsNotice(QStringLiteral("Installing download tools…"), QStringLiteral("neutral"),
                                    QStringLiteral("yt-dlp and Deno install by themselves. Links you add start when they are ready."));
        break;
    case ToolsManager::Status::Updating:
        m_tabHeader->setToolsNotice(QStringLiteral("Updating download tools…"), QStringLiteral("neutral"),
                                    QStringLiteral("Checking for new versions of yt-dlp and Deno. Downloads keep working."));
        break;
    case ToolsManager::Status::Missing:
        m_tabHeader->setToolsNotice(QStringLiteral("Download tools missing · Retry"), QStringLiteral("err"),
                                    QStringLiteral("The automatic install failed. Check your connection and click to retry."));
        break;
    }
    const bool ready = m_toolsManager->areToolsInstalled();
    m_queueView->setWaitingForTools(!ready);
    if (ready && m_downloadQueue) {
        m_downloadQueue->kick();
    }
}

void MainWindow::onVideoPasswordRequired(const DownloadItem &item)
{
    VideoPasswordDialog dialog(item.url, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.getVideoPassword().isEmpty()) {
        m_downloadQueue->retryDownloadWithVideoPassword(dialog.getVideoPassword());
        return;
    }
    m_downloadQueue->abandonPasswordRequest();
}

void MainWindow::onShowRequested(int id)
{
    const DownloadItem *item = m_downloadQueue ? m_downloadQueue->item(id) : nullptr;
    if (!item) {
        return;
    }
    const QString file = item->filePath;
    if (!file.isEmpty() && QFileInfo::exists(file)) {
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(file)});
        return;
#elif defined(Q_OS_MAC)
        QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), file});
        return;
#endif
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(item->options.downloadDir));
}

void MainWindow::onCopyErrorRequested(int id)
{
    const DownloadItem *item = m_downloadQueue ? m_downloadQueue->item(id) : nullptr;
    if (!item) {
        return;
    }
    QApplication::clipboard()->setText(QStringLiteral("%1\n%2\n\n%3").arg(item->url, item->errorHeadline,
                                                                          item->errorMessage.trimmed()));
}

void MainWindow::onRetryRequested(int id)
{
    const DownloadItem *item = m_downloadQueue ? m_downloadQueue->item(id) : nullptr;
    if (!item) {
        return;
    }
    // Formato, calidad y carpeta del item; la sesion, la elegida ahora (el arreglo tipico
    // de un error de sesion es cambiar Use cookies from y reintentar). Un item con la sesion
    // de la extension conserva esas cookies: el combo no es su fuente.
    DownloadOptions options = item->options;
    if (!options.hasSession()) {
        const DownloadOptions now = currentOptions();
        options.cookiesBrowser = now.cookiesBrowser;
        options.cookiesFile = now.cookiesFile;
    }
    m_downloadQueue->retryItem(id, options);
}

void MainWindow::onRetryWithBrowser(int id, const QString &browserKey)
{
    onCookiesSourceActivated(browserKey);
    onRetryRequested(id);
}

QJsonObject MainWindow::handleBrowserRequest(const NativeHost::Request &request)
{
    const auto failure = [](const QString &message) {
        return QJsonObject{{QStringLiteral("v"), NativeHost::kProtocolVersion}, {QStringLiteral("ok"), false},
                           {QStringLiteral("error"), QStringLiteral("internal")}, {QStringLiteral("message"), message}};
    };
    if (!m_downloadQueue) {
        return failure(QStringLiteral("The app isn't ready yet. Try again."));
    }
    // Opciones actuales de la app (carpeta, formato, calidad). Sin dialogos modales: el pedido
    // lo dispara el navegador y la respuesta vuelve al popup.
    DownloadOptions options = currentOptions();
    if (!isValidDownloadPath(options.downloadDir)) {
        log(QStringLiteral("Browser extension: choose a download folder to receive links"), LogLevel::Warning);
        return failure(QStringLiteral("Choose a download folder in the app"));
    }

    const QString key = request.browser.toLower();
    options.fromBrowser = key == QLatin1String("brave") ? QStringLiteral("Brave")
                          : key == QLatin1String("edge") ? QStringLiteral("Edge")
                          : key == QLatin1String("chrome") ? QStringLiteral("Chrome")
                                                           : QStringLiteral("Browser");
    int accepted = 0;
    if (!request.cookies.isEmpty()) {
        const QByteArray cookies = SessionCookies::toNetscape(request.cookies, &accepted);
        if (accepted > 0) {
            // La sesion de la extension reemplaza a "Use cookies from" solo para este item.
            options.sessionCookies = cookies;
            options.sessionCookieCount = accepted;
            options.cookiesBrowser.clear();
            options.cookiesFile.clear();
        }
    }
    m_downloadQueue->addDownload(request.url, options);

    // Solo cantidades: nunca nombres ni valores de cookies.
    const QString session = options.hasSession()
        ? QStringLiteral("%1 session (%2 cookies)").arg(options.fromBrowser).arg(accepted)
        : request.hasCookies ? QStringLiteral("no cookies for this site") : QStringLiteral("without the browser session");
    log(QStringLiteral("Added from %1 · %2 · %3").arg(options.fromBrowser, request.url, session));
    // Sin robar el foco: la barra de tareas avisa.
    if (!isActiveWindow()) {
        QApplication::alert(this);
    }
    return QJsonObject{{QStringLiteral("v"), NativeHost::kProtocolVersion}, {QStringLiteral("ok"), true},
                       {QStringLiteral("status"), QStringLiteral("queued")}};
}

void MainWindow::bringToFront()
{
    if (isMinimized()) {
        showNormal();
    }
    show();
    raise();
    activateWindow();
}

UpdateView MainWindow::currentUpdateView() const
{
    UpdateView view;
    view.currentVersion = QStringLiteral(VIDEODOWNLOADER_VERSION);
    view.installsInPlace = UpdateService::installsInPlace();
    if (m_updateService) {
        view.state = m_updateService->state();
        view.availableVersion = m_updateService->availableVersion();
        view.error = m_updateService->errorString();
        view.blockedReason = m_updateService->installBlockedReason();
    }
    view.received = m_installReceived;
    view.total = m_installTotal;
    view.lastChecked = m_lastUpdateCheck;
    return view;
}

void MainWindow::refreshUpdateNotice()
{
    if (!m_updateService) {
        return;
    }
    QString text;
    switch (m_updateService->state()) {
    case UpdateService::State::UpdateAvailable:
        text = QStringLiteral("Update available · v%1").arg(m_updateService->availableVersion());
        break;
    case UpdateService::State::Downloading:
        text = QStringLiteral("Downloading update…");
        break;
    case UpdateService::State::Installing:
        text = QStringLiteral("Installing update…");
        break;
    case UpdateService::State::InstallFailed:
        text = QStringLiteral("Update failed");
        break;
    default:
        break;
    }
    m_tabHeader->setUpdateNotice(text);
    if (m_helpDialog) {
        m_helpDialog->setUpdateView(currentUpdateView());
    }
}

void MainWindow::openHelp()
{
    if (m_helpDialog) {
        return;
    }
    HelpDialog dialog(this);
    m_helpDialog = &dialog;
    if (m_toolsManager) {
        dialog.setToolVersions(m_toolsManager->toolVersions());
        m_toolsManager->refreshToolVersions();
    }
    dialog.setUpdateView(currentUpdateView());
    connect(&dialog, &HelpDialog::checkRequested, this, [this]() {
        if (m_updateService) {
            m_updateService->checkForUpdates();
        }
    });
    connect(&dialog, &HelpDialog::installRequested, this, &MainWindow::requestAppInstall);
    connect(&dialog, &HelpDialog::openExtensionFolderRequested, this, [this]() {
        const QString folder = HostRegistration::extensionFolder();
        if (!QFileInfo(folder).isDir()) {
            log(QStringLiteral("Extension folder not found: %1").arg(QDir::toNativeSeparators(folder)), LogLevel::Warning);
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    connect(&dialog, &HelpDialog::cancelInstallRequested, this, [this]() {
        if (m_updateService) {
            m_updateService->cancelInstall();
        }
    });
    dialog.execOver(this);
    m_helpDialog = nullptr;
}

void MainWindow::requestAppInstall()
{
    if (!m_updateService) {
        return;
    }
    const int active = m_downloadQueue ? m_downloadQueue->activeDownloadCount() : 0;
    QWidget *parent = m_helpDialog ? static_cast<QWidget *>(m_helpDialog.data()) : this;
    if (active > 0 && UpdateService::installsInPlace()) {
        const auto answer = QMessageBox::question(parent, QStringLiteral("Update"),
            QStringLiteral("Updating will stop %1 active download(s). Continue?").arg(active),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    m_installReceived = -1;
    m_installTotal = -1;
    m_updateService->installAppUpdate();
}

bool MainWindow::isValidDownloadPath(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QDir dir(path);
    return dir.exists() && dir.isReadable();
}

QString MainWindow::configPath()
{
    // Misma carpeta que PipeSync: <AppData>/LGA/VideoDownloader/config.ini
    QString appDataPath;
#ifdef Q_OS_WIN
    appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    appDataPath = appDataPath.replace("/VideoDownloader", "").replace("\\VideoDownloader", "");
    appDataPath += "/VideoDownloader";
#elif defined(Q_OS_MAC)
    appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    appDataPath = appDataPath.replace("/VideoDownloader", "");
    appDataPath += "/VideoDownloader";
#else
    appDataPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    appDataPath += "/LGA/VideoDownloader";
#endif
    QDir dir(appDataPath);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return appDataPath + "/config.ini";
}

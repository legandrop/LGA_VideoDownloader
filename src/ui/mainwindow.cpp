#include "videodownloader/mainwindow.h"
#include "videodownloader/colorutils.h"
#include "videodownloader/toolsmanager.h"
#include "videodownloader/downloadqueue.h"
#include "videodownloader/updateservice.h"
#include "videodownloader/videopassworddialog.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QGroupBox>
#include <QMessageBox>
#include <QUrl>
#include <QDesktopServices>
#include <QFileDialog>
#include <QStandardPaths>
#include <QTimer>
#include <QScreen>
#include <QStyle>
#include <QStandardPaths>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QMouseEvent>
#include <QEvent>
#include <QDebug>


// Constante para mantener consistencia de ancho del grupo settings
constexpr int SETTINGS_GROUP_WIDTH = 520;

// Retraso del auto-update (tools y app) despues de construir la ventana: deja que la UI
// termine de aparecer antes de meter trafico de red y procesos.
constexpr int AUTO_UPDATE_DELAY_MS = 2000;


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_centralWidget(nullptr)
    , m_mainLayout(nullptr)
    , m_inputGroup(nullptr)
    , m_inputLayout(nullptr)
    , m_urlLayout(nullptr)
    , m_urlInput(nullptr)
    , m_downloadButton(nullptr)
    , m_progressGroup(nullptr)
    , m_progressLayout(nullptr)
    , m_progressButtonLayout(nullptr)
    , m_progressBar(nullptr)
    , m_progressLabel(nullptr)
    , m_cancelButton(nullptr)
    , m_logGroup(nullptr)
    , m_logLayout(nullptr)
    , m_logOutput(nullptr)
    , m_logExpanded(false)
    , m_settingsGroup(nullptr)
    , m_settingsLayout(nullptr)
    , m_settingsExpanded(false)
    , m_credentialsLayout(nullptr)
    , m_folderLayout(nullptr)
    , m_toolsLayout(nullptr)
    , m_cookiesLabel(nullptr)
    , m_cookiesSourceCombo(nullptr)
    , m_downloadFolderInput(nullptr)
    , m_browseFolderButton(nullptr)
    , m_toolsButton(nullptr)
    , m_settings(nullptr)
    , m_toolsManager(nullptr)
    , m_downloadQueue(nullptr)
    , m_updateService(nullptr)
    , m_updateLinkButton(nullptr)
    , m_maxWindowWidth(550) // Ancho mínimo para evitar problemas cuando settings inicia colapsado
{
    // Inicializar configuración
    m_settings = new QSettings(getConfigPath(), QSettings::IniFormat, this);
    
    setupUI();
    setupStyles();
    setupConnections();
    loadSettings();
    detectOperatingSystem();
    
    // Initialize tools manager
    m_toolsManager = new ToolsManager(m_logOutput, m_toolsButton, this);
    connect(m_toolsManager, &ToolsManager::toolsStatusChanged, this, &MainWindow::onToolsStatusChanged);
    connect(m_toolsManager, &ToolsManager::toolsStatusChanged, this, &MainWindow::onToolsStatusChangedForInitialState);
    m_toolsManager->checkToolsInstallation();

    // Set initial settings state based on credentials (tools status will be handled by signal)
    setInitialSettingsState();
    
    // Initialize download queue
    m_downloadQueue = new DownloadQueue(m_logOutput, m_progressBar, m_progressGroup, m_toolsManager, this);
    connect(m_downloadQueue, &DownloadQueue::downloadStarted, this, &MainWindow::onDownloadStarted);
    connect(m_downloadQueue, &DownloadQueue::downloadCompleted, this, &MainWindow::onDownloadCompleted);
    connect(m_downloadQueue, &DownloadQueue::queueStatusChanged, this, &MainWindow::onQueueStatusChanged);
    connect(m_downloadQueue, &DownloadQueue::downloadAddedToQueue, this, &MainWindow::onDownloadAddedToQueue);
    connect(m_downloadQueue, &DownloadQueue::videoPasswordRequired, this, &MainWindow::onVideoPasswordRequired);

    // El swap de tools espera a que no haya un yt-dlp corriendo.
    m_toolsManager->setProcessActiveProbe([this]() {
        return m_downloadQueue && m_downloadQueue->hasActiveProcess();
    });

    // Update de la app: el hook corta la cola y mata yt-dlp con sus hijos antes del instalador.
    m_updateService = new UpdateService(this);
    m_updateService->setBeforeInstallHook([this]() {
        if (m_downloadQueue) {
            m_downloadQueue->stopAllForShutdown();
        }
    });
    connect(m_updateService, &UpdateService::stateChanged, this, &MainWindow::refreshUpdateLink);
    connect(m_toolsManager, &ToolsManager::toolVersionsChanged, this, &MainWindow::refreshUpdateLink);
    refreshUpdateLink();

    // yt-dlp y deno se instalan/actualizan solos y en silencio; la app solo chequea.
    QTimer::singleShot(AUTO_UPDATE_DELAY_MS, this, [this]() {
        m_toolsManager->startAutomaticUpdate();
        m_toolsManager->refreshToolVersions();
        m_updateService->checkForUpdates();
    });
    
    // Configurar ventana
    // La version sale de la macro del CMakeLists, nunca de un literal: hardcodeada
    // aca quedaba desfasada en cuanto alguien bumpeaba el proyecto.
    setWindowTitle(QStringLiteral("LGA_VideoDownloader v" VIDEODOWNLOADER_VERSION));

    // Ajustar tamaño inicial y establecer ancho máximo
    adjustWindowSize();
    // Después del ajuste inicial, aseguramos que el ancho máximo esté establecido
    // y que todos los widgets estén completamente inicializados
    QTimer::singleShot(500, this, [this]() {
        adjustWindowSize();
    });
    
    // Centrar ventana en pantalla
    move(QApplication::primaryScreen()->geometry().center() - frameGeometry().center());
}

MainWindow::~MainWindow()
{
    // Los widgets se limpian automáticamente por Qt
}

void MainWindow::setupUI()
{
    // Widget central
    m_centralWidget = new QWidget(this);
    m_centralWidget->setObjectName("centralWidget");
    setCentralWidget(m_centralWidget);
    
    // Layout principal
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setSpacing(16);
    m_mainLayout->setContentsMargins(16, 20, 12, 20);

    // Establecer restricción fija para evitar redimensionamiento automático
    // pero permitir ajustes manuales cuando cambie la visibilidad de widgets internos (como el log)
    m_mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    
    // Video URL Group
    m_inputGroup = new QGroupBox("Video URL", this);
    // Política de tamaño que permite ajuste mínimo pero mantiene estabilidad
    m_inputGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_inputLayout = new QVBoxLayout(m_inputGroup);
    m_inputLayout->setSpacing(8);
    
    // Layout horizontal para URL y botón
    m_urlLayout = new QHBoxLayout();
    m_urlInput = new QLineEdit(this);
    m_urlInput->setPlaceholderText("https://vimeo.com/... or https://youtube.com/...");
    m_urlInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_downloadButton = new QPushButton("Download", this);
    m_downloadButton->setEnabled(true); // Lo vamos a dejar SIEMPRE EN TRUE. NO CAMBIAR!!!!!
    m_downloadButton->setFixedWidth(110);

    m_urlLayout->addWidget(m_urlInput);
    m_urlLayout->addWidget(m_downloadButton);
    
    m_inputLayout->addLayout(m_urlLayout);
    
    // Progress Group
    m_progressGroup = new QGroupBox("Progress (0/0)", this);
    // Política de tamaño que permite ajuste mínimo pero mantiene estabilidad
    m_progressGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_progressLayout = new QVBoxLayout(m_progressGroup);
    m_progressLayout->setSpacing(8);
    
    // Progress bar and cancel button in same line (like URL layout)
    m_progressButtonLayout = new QHBoxLayout();
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false); // Hide percentage text when inactive
    m_progressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_cancelButton = new QPushButton("Cancel", this);
    m_cancelButton->setObjectName("cancelButton");
    m_cancelButton->setFixedWidth(110); // Same width as download button
    // No danger class - same color as other buttons

    m_progressButtonLayout->addWidget(m_progressBar);
    m_progressButtonLayout->addWidget(m_cancelButton);
    
    // Store reference to the group box title for updates
    m_progressLabel = nullptr; // We'll use the group box title instead
    
    m_progressLayout->addLayout(m_progressButtonLayout);
    
    // Log Group - restored to original with clickable title (starts collapsed)
    m_logGroup = new QGroupBox("Log >", this);
    m_logGroup->setObjectName("logGroupBox");
    m_logGroup->setProperty("collapsed", true); // Set collapsed property for CSS
    m_logGroup->setCursor(Qt::PointingHandCursor);
    m_logGroup->setFixedHeight(35); // Altura aumentada en 10px más
    m_logLayout = new QVBoxLayout(m_logGroup);
    m_logLayout->setContentsMargins(0, 0, 0, 0); // Sin márgenes cuando colapsado
    m_logLayout->setSpacing(0); // No spacing between widgets
    
    m_logOutput = new QTextEdit(this);
    m_logOutput->setReadOnly(true);
    m_logOutput->setMinimumHeight(200);
    m_logOutput->setMaximumHeight(250);
    m_logOutput->setFont(QFont("Courier", 10));
    m_logOutput->hide(); // Start hidden
    
    m_logLayout->addWidget(m_logOutput);
    
    // Settings Group - clickable like log group (starts expanded)
    m_settingsGroup = new QGroupBox("Settings ⌄", this);
    m_settingsGroup->setObjectName("settingsGroupBox");
    m_settingsGroup->setProperty("collapsed", false); // Not collapsed initially
    m_settingsGroup->setCursor(Qt::PointingHandCursor);
    // Política de tamaño que permite ajuste mínimo pero mantiene estabilidad
    m_settingsGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_settingsLayout = new QVBoxLayout(m_settingsGroup);
    m_settingsLayout->setSpacing(8);
    // Agregar padding interno consistente con otras secciones cuando esté expandido
    m_settingsLayout->setContentsMargins(10, 10, 10, 4);
    
    // Primera fila: Login | origen de cookies.
    // Reemplaza a usuario/contrasena: la app ya no le pide a nadie las credenciales de su
    // cuenta, usa la sesion ya iniciada en un navegador. UI minima a proposito: el diseno
    // definitivo de esta fila se resuelve aparte. El dato de cada item es el nombre que
    // espera --cookies-from-browser; "file" abre un selector de cookies.txt.
    m_credentialsLayout = new QHBoxLayout();
    m_cookiesLabel = new QLabel("Login:", this);
    m_cookiesSourceCombo = new QComboBox(this);
    m_cookiesSourceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_cookiesSourceCombo->addItem("None (public videos only)", QString());
    m_cookiesSourceCombo->addItem("Firefox session (recommended on Windows)", QStringLiteral("firefox"));
    m_cookiesSourceCombo->addItem("Chrome session", QStringLiteral("chrome"));
    m_cookiesSourceCombo->addItem("Edge session", QStringLiteral("edge"));
    m_cookiesSourceCombo->addItem("Brave session", QStringLiteral("brave"));
    m_cookiesSourceCombo->addItem("Opera session", QStringLiteral("opera"));
    m_cookiesSourceCombo->addItem("Vivaldi session", QStringLiteral("vivaldi"));
#ifdef Q_OS_MAC
    m_cookiesSourceCombo->addItem("Safari session", QStringLiteral("safari"));
#endif
    m_cookiesSourceCombo->addItem("cookies.txt file...", QStringLiteral("file"));
    m_cookiesSourceCombo->setToolTip("Downloads use the account you are already signed in to in this browser.\n"
                                     "Chromium browsers (Chrome, Edge, Brave) must be fully closed on Windows,\n"
                                     "and may still fail because they encrypt their cookies.");

    m_credentialsLayout->addWidget(m_cookiesLabel);
    m_credentialsLayout->addWidget(m_cookiesSourceCombo);

    // Agregar padding interno consistente con otros grupos
    m_credentialsLayout->setContentsMargins(10, 4, 10, 4);
    
    // Second row: Download Folder | Browse
    m_folderLayout = new QHBoxLayout();
    m_downloadFolderInput = new QLineEdit(this);
    m_downloadFolderInput->setPlaceholderText("Download Folder...");
    m_downloadFolderInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_browseFolderButton = new QPushButton("Browse", this);
    m_browseFolderButton->setFixedWidth(110);

    m_folderLayout->addWidget(m_downloadFolderInput);
    m_folderLayout->addWidget(m_browseFolderButton);

    // Agregar padding interno consistente con otros grupos
    m_folderLayout->setContentsMargins(10, 4, 10, 4);
    
    // Third row: tools button aligned right
    m_toolsLayout = new QHBoxLayout();
    m_toolsButton = new QPushButton("Checking Tools...", this);
    m_toolsButton->setObjectName("toolsButton");
    m_toolsButton->setEnabled(false);
    m_toolsButton->setFixedWidth(110);

    // Usar un widget spacer fijo en lugar de addStretch() para evitar recálculos
    QWidget *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    spacer->setFixedHeight(0);

    // provisional: lo reemplaza el rediseño. Version de la app y estado del update; al
    // hacer click chequea o instala segun el estado. Las versiones de las tools van en el
    // tooltip. Es un boton plano y no un link para poder accionarlo por accesibilidad.
    m_updateLinkButton = new QPushButton(this);
    m_updateLinkButton->setFlat(true);
    m_updateLinkButton->setCursor(Qt::PointingHandCursor);
    connect(m_updateLinkButton, &QPushButton::clicked, this, &MainWindow::onUpdateLinkClicked);

    m_toolsLayout->addWidget(m_updateLinkButton);
    m_toolsLayout->addWidget(spacer);
    m_toolsLayout->addWidget(m_toolsButton);

    // Agregar padding interno consistente con otros grupos
    m_toolsLayout->setContentsMargins(10, 4, 10, 10);
    
    m_settingsLayout->addLayout(m_credentialsLayout);
    m_settingsLayout->addLayout(m_folderLayout);
    m_settingsLayout->addLayout(m_toolsLayout);
    
    // Agregar todos los grupos al layout principal
    m_mainLayout->addWidget(m_inputGroup);
    m_mainLayout->addWidget(m_progressGroup);
    m_mainLayout->addWidget(m_settingsGroup);
    m_mainLayout->addWidget(m_logGroup);
    
    // Agregar un spacer al final para empujar todo hacia arriba cuando el log está colapsado
    m_mainLayout->addStretch();
}

void MainWindow::setupStyles()
{
    // Forzar actualización de estilos para todos los botones
    style()->unpolish(m_downloadButton);
    style()->polish(m_downloadButton);

    style()->unpolish(m_browseFolderButton);
    style()->polish(m_browseFolderButton);

    style()->unpolish(m_toolsButton);
    style()->polish(m_toolsButton);

    style()->unpolish(m_cancelButton);
    style()->polish(m_cancelButton);
}

void MainWindow::setupConnections()
{
    // Connect UI signals
    connect(m_urlInput, &QLineEdit::textChanged, this, &MainWindow::onUrlChanged);
    connect(m_urlInput, &QLineEdit::returnPressed, this, &MainWindow::onDownloadClicked);
    connect(m_downloadButton, &QPushButton::clicked, this, &MainWindow::onDownloadClicked);
    connect(m_cookiesSourceCombo, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::onCookiesSourceChanged);
    connect(m_browseFolderButton, &QPushButton::clicked, this, &MainWindow::onBrowseFolderClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::onCancelClicked);

    // Install event filter for log group box to capture clicks
    m_logGroup->installEventFilter(this);

    // Install event filter for settings group box to capture clicks
    m_settingsGroup->installEventFilter(this);
}

void MainWindow::onDownloadClicked()
{
    QString url = m_urlInput->text().trimmed();
    QString downloadDir = m_settings->value("download/folder", "").toString();
    
    // 1. Validate URL is not empty
    if (url.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please enter a valid URL.");
        return;
    }
    
    // 2. Validate video URL (Vimeo or YouTube)
    if (!isValidVideoUrl(url)) {
        QMessageBox::warning(this, "Error", "Please enter a valid Vimeo or YouTube URL.");
        return;
    }
    
    // 3. Origen de cookies. No se bloquea si falta: hay videos publicos, y si el sitio
    //    exige sesion, el log explica que elegir (ver DownloadQueue::logFailureHint).
    QString cookiesBrowser = m_settings->value("auth/cookiesBrowser", QString()).toString();
    QString cookiesFile = m_settings->value("auth/cookiesFile", "").toString();
    if (cookiesBrowser == QLatin1String("file")) {
        if (cookiesFile.isEmpty() || !QFileInfo::exists(cookiesFile)) {
            QMessageBox::warning(this, "Error", "The selected cookies.txt file does not exist. Please choose it again in Settings.");
            return;
        }
        cookiesBrowser.clear();
    } else {
        cookiesFile.clear();
    }

    // 4. Validate download folder exists
    if (downloadDir.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please set a download folder first.");
        return;
    }
    
    if (!isValidDownloadPath(downloadDir)) {
        QMessageBox::warning(this, "Error", "Download folder does not exist or is not accessible. Please select a valid folder.");
        return;
    }
    
    // 5. Check that both tools are installed
    if (!m_toolsManager->areToolsInstalled()) {
        QMessageBox::warning(this, "Error", "Required tools (yt-dlp and ffmpeg) are not installed. Please install them first using the Tools button.");
        return;
    }
    
    // Add to download queue
    m_downloadQueue->addDownload(url, cookiesBrowser, cookiesFile, downloadDir);
    
    // Clear URL input for next download
    m_urlInput->clear();
}

void MainWindow::onUrlChanged()
{
    // Note: This function used to enable/disable the download button based on validation
    // Now the download button is always enabled and validation happens in onDownloadClicked()
    // This function is kept for potential future UI updates but currently does nothing
}

void MainWindow::onToolsStatusChanged(bool allInstalled)
{
    // Tools status changed - update UI
    onUrlChanged();
}

void MainWindow::onDownloadStarted()
{
    // Note: Download button remains enabled - user can queue multiple downloads
    // m_downloadButton->setEnabled(false); // REMOVED - button always enabled
}

void MainWindow::onDownloadCompleted()
{
    // Download completed - update UI state
    onUrlChanged();
}

void MainWindow::onQueueStatusChanged(int current, int total)
{
    // Update progress group title
    m_progressGroup->setTitle(QString("Progress (%1/%2)").arg(current).arg(total));
}

void MainWindow::onDownloadAddedToQueue(int totalCount)
{
    // When a download is added, only update the total count, keep current number unchanged
    QString currentTitle = m_progressGroup->title();
    QRegularExpression regex("Progress \\((\\d+)/(\\d+)\\)");
    QRegularExpressionMatch match = regex.match(currentTitle);

    int currentNumber = 0;
    if (match.hasMatch()) {
        currentNumber = match.captured(1).toInt();
    }

    // Update only the total count, keep current number
    m_progressGroup->setTitle(QString("Progress (%1/%2)").arg(currentNumber).arg(totalCount));
}

void MainWindow::onVideoPasswordRequired(const DownloadItem &item)
{
    // Show video password dialog
    VideoPasswordDialog dialog(item.url, this);
    int result = dialog.exec();

    if (result == QDialog::Accepted) {
        QString videoPassword = dialog.getVideoPassword();
        if (!videoPassword.isEmpty()) {
            // Retry download with video password
            m_downloadQueue->retryDownloadWithVideoPassword(videoPassword);
            return;
        }
    }

    // If dialog was cancelled or password was empty, mark download as failed
    // We need to manually handle the failure since we intercepted the normal flow
    m_logOutput->append("ERROR: Video password not provided or download cancelled");

    // Manually trigger the next download processing
    QTimer::singleShot(1000, [this]() {
        if (m_downloadQueue) {
            // Reset current download state and continue
            QMetaObject::invokeMethod(m_downloadQueue, "processNextDownload", Qt::QueuedConnection);
        }
    });
}

void MainWindow::onCancelClicked()
{
    if (m_downloadQueue) {
        // Reset entire queue and all counters
        m_downloadQueue->resetQueue();
        
        // Update UI state
        onUrlChanged();
    }
}

void MainWindow::onCookiesSourceChanged(int index)
{
    QString source = m_cookiesSourceCombo->itemData(index).toString();

    if (source == QLatin1String("file")) {
        QString startDir = QFileInfo(m_settings->value("auth/cookiesFile", "").toString()).absolutePath();
        QString file = QFileDialog::getOpenFileName(this, "Select cookies.txt (Netscape format)", startDir,
                                                    "Cookies files (*.txt);;All files (*)");
        if (file.isEmpty()) {
            // Cancelado: volver a mostrar lo que estaba guardado
            loadSettings();
            return;
        }
        m_settings->setValue("auth/cookiesFile", file);
        m_cookiesSourceCombo->setToolTip(file);
        m_logOutput->append(QString("Login saved: cookies file %1").arg(file));
    } else if (source.isEmpty()) {
        m_logOutput->append("Login saved: none (public videos only)");
    } else {
        m_logOutput->append(QString("Login saved: cookies from %1").arg(source));
#ifdef Q_OS_WIN
        if (source != QLatin1String("firefox")) {
            m_logOutput->append("Note: on Windows this browser must be fully closed while downloading, "
                                "and its encrypted cookies may not be readable. Firefox is the reliable option.");
        }
#endif
    }

    m_settings->setValue("auth/cookiesBrowser", source);
    m_settings->sync();
    onUrlChanged();
}


void MainWindow::onBrowseFolderClicked()
{
    QString currentFolder = m_settings->value("download/folder", QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)).toString();
    
    QString folder = QFileDialog::getExistingDirectory(this, "Select Download Folder", currentFolder);
    
    if (!folder.isEmpty()) {
        m_downloadFolderInput->setText(folder);
        
        // Auto-save the selected folder
        m_settings->setValue("download/folder", folder);
        m_settings->sync();
        
        m_logOutput->append(QString("Download folder saved: %1").arg(folder));
        onUrlChanged();
    }
}

void MainWindow::loadSettings()
{
    // Sin eleccion explicita no se usan cookies, en las dos plataformas: un navegador por
    // defecto fallaba en Windows (Chromium no es legible) y en macOS se aplicaba tambien a Vimeo.
    QString cookiesSource = m_settings->value("auth/cookiesBrowser", QString()).toString();
    QString downloadFolder = m_settings->value("download/folder", "").toString();

    int cookiesIndex = m_cookiesSourceCombo->findData(cookiesSource);
    if (cookiesIndex < 0) {
        // Valor guardado que este combo no ofrece (config vieja, "safari" traido de macOS,
        // edicion a mano): se normaliza a "None" en el config para no pasarle a yt-dlp algo
        // distinto de lo que la UI muestra.
        qWarning() << "Origen de cookies guardado no valido, se normaliza a ninguno:" << cookiesSource;
        m_settings->setValue("auth/cookiesBrowser", QString());
        m_settings->sync();
        cookiesSource.clear();
        cookiesIndex = 0;
    }
    m_cookiesSourceCombo->setCurrentIndex(cookiesIndex);
    if (cookiesSource == QLatin1String("file")) {
        m_cookiesSourceCombo->setToolTip(m_settings->value("auth/cookiesFile", "").toString());
    }

    // Only set text if values exist, otherwise keep placeholders
    if (!downloadFolder.isEmpty()) {
        m_downloadFolderInput->setText(downloadFolder);
    }
}

bool MainWindow::shouldShowSettingsExpanded()
{
    // Settings debe abrir expandido si:
    
    // 1. La carpeta de destino está vacía o no es válida
    QString downloadDir = m_settings->value("download/folder", "").toString();
    bool downloadDirInvalid = downloadDir.isEmpty() || !isValidDownloadPath(downloadDir);

    // 2. O si las herramientas no están instaladas
    bool toolsNotInstalled = m_toolsManager && !m_toolsManager->areToolsInstalled();

    // Note: No longer checking for Vimeo credentials here since they're only needed for Vimeo URLs
    return downloadDirInvalid || toolsNotInstalled;
}

void MainWindow::setInitialSettingsState()
{
    // Determinar estado inicial basado en la carpeta de destino (las herramientas llegan por signal).
    // El login ya no fuerza la expansion: es opcional y hay videos publicos.
    QString downloadDir = m_settings->value("download/folder", "").toString();

    bool downloadDirEmpty = downloadDir.isEmpty();

    // Settings inicia expandido si no hay carpeta de destino
    m_settingsExpanded = downloadDirEmpty;

    // Configurar estado visual inicial
    if (m_settingsExpanded) {
        m_settingsGroup->setTitle("Settings ⌄");
        m_settingsGroup->setProperty("collapsed", false);
        m_settingsGroup->setFixedHeight(QWIDGETSIZE_MAX);
        m_settingsGroup->setMinimumHeight(0);
        m_settingsGroup->setMaximumHeight(QWIDGETSIZE_MAX);
        // Mantener ancho consistente cuando está expandido
        m_settingsGroup->setMinimumWidth(SETTINGS_GROUP_WIDTH);
        m_settingsGroup->setMaximumWidth(SETTINGS_GROUP_WIDTH);
        m_settingsLayout->setContentsMargins(3, 2, 3, 3);
        m_settingsLayout->setSpacing(8);
        // Show all settings widgets
        m_cookiesLabel->show();
        m_cookiesSourceCombo->show();
        m_downloadFolderInput->show();
        m_browseFolderButton->show();
        m_toolsButton->show();
        m_updateLinkButton->show(); // provisional: lo reemplaza el rediseño
    } else {
        m_settingsGroup->setTitle("Settings >");
        m_settingsGroup->setProperty("collapsed", true);
        m_settingsGroup->setFixedHeight(35);
        // Establecer ancho mínimo fijo para evitar que otros grupos se contraigan
        // Basado en el tamaño típico cuando está expandido con todos los controles
        m_settingsGroup->setMinimumWidth(SETTINGS_GROUP_WIDTH); // Ancho conservador para settings expandido
        m_settingsGroup->setMaximumWidth(SETTINGS_GROUP_WIDTH);
        m_settingsLayout->setContentsMargins(0, 0, 0, 0);
        m_settingsLayout->setSpacing(0);
        // Hide all settings widgets initially
        m_cookiesLabel->hide();
        m_cookiesSourceCombo->hide();
        m_downloadFolderInput->hide();
        m_browseFolderButton->hide();
        m_toolsButton->hide();
        m_updateLinkButton->hide(); // provisional: lo reemplaza el rediseño
    }

    // Force style refresh to apply new property
    m_settingsGroup->style()->unpolish(m_settingsGroup);
    m_settingsGroup->style()->polish(m_settingsGroup);
}

void MainWindow::onToolsStatusChangedForInitialState(bool allInstalled)
{
    // Si las herramientas no están instaladas, asegurar que settings esté expandido
    if (!allInstalled && !m_settingsExpanded) {
        m_settingsExpanded = true;

        m_settingsGroup->setTitle("Settings ⌄");
        m_settingsGroup->setProperty("collapsed", false);
        m_settingsGroup->setFixedHeight(QWIDGETSIZE_MAX);
        m_settingsGroup->setMinimumHeight(0);
        m_settingsGroup->setMaximumHeight(QWIDGETSIZE_MAX);
        // Mantener ancho consistente cuando está expandido
        m_settingsGroup->setMinimumWidth(SETTINGS_GROUP_WIDTH);
        m_settingsGroup->setMaximumWidth(SETTINGS_GROUP_WIDTH);
        m_settingsLayout->setContentsMargins(3, 2, 3, 3);
        m_settingsLayout->setSpacing(8);

        // Show all settings widgets
        m_cookiesLabel->show();
        m_cookiesSourceCombo->show();
        m_downloadFolderInput->show();
        m_browseFolderButton->show();
        m_toolsButton->show();
        m_updateLinkButton->show(); // provisional: lo reemplaza el rediseño

        // Force style refresh to apply new property
        m_settingsGroup->style()->unpolish(m_settingsGroup);
        m_settingsGroup->style()->polish(m_settingsGroup);

        // Adjust window size after expanding settings
        QTimer::singleShot(100, this, &MainWindow::adjustWindowSize);
    }
}

bool MainWindow::isValidVideoUrl(const QString &url) const
{
    if (url.isEmpty()) {
        return false;
    }
    
    // Check for Vimeo URLs
    if (url.contains("vimeo.com", Qt::CaseInsensitive)) {
        return true;
    }
    
    // Check for YouTube URLs
    if (url.contains("youtube.com", Qt::CaseInsensitive) || 
        url.contains("youtu.be", Qt::CaseInsensitive)) {
        return true;
    }
    
    return false;
}

bool MainWindow::isVimeoUrl(const QString &url) const
{
    if (url.isEmpty()) {
        return false;
    }
    
    return url.contains("vimeo.com", Qt::CaseInsensitive);
}

bool MainWindow::isValidDownloadPath(const QString &path) const
{
    if (path.isEmpty()) {
        return false;
    }
    
    QDir dir(path);
    return dir.exists() && dir.isReadable();
}

QString MainWindow::getConfigPath() const
{
    // Crear la carpeta de configuración siguiendo el patrón de PipeSync
    QString appDataPath;
    
#ifdef Q_OS_WIN
    // Windows: %APPDATA%\LGA\VideoDownloader\config.ini
    appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    appDataPath = appDataPath.replace("/VideoDownloader", "").replace("\\VideoDownloader", "");
    appDataPath += "/VideoDownloader";
#elif defined(Q_OS_MAC)
    // macOS: ~/Library/Application Support/LGA/VideoDownloader/config.ini
    appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    appDataPath = appDataPath.replace("/VideoDownloader", "");
    appDataPath += "/VideoDownloader";
#else
    // Linux: ~/.config/LGA/VideoDownloader/config.ini
    appDataPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    appDataPath += "/LGA/VideoDownloader";
#endif
    
    QDir dir(appDataPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    return appDataPath + "/config.ini";
}

void MainWindow::detectOperatingSystem()
{
#ifdef Q_OS_MAC
    m_logOutput->append("=== System Information ===");
    m_logOutput->append("Operating System: macOS");
    m_logOutput->append("Tools installation method: Download from GitHub");
    m_logOutput->append("Tools location: user data folder (updated automatically)");
    m_logOutput->append("Supported platforms: Vimeo, YouTube");
    m_logOutput->append("===========================");
#elif defined(Q_OS_WIN)
    m_logOutput->append("=== System Information ===");
    m_logOutput->append("Operating System: Windows");
    m_logOutput->append("Tools installation method: Download from GitHub");
    m_logOutput->append("Tools location: user data folder (updated automatically)");
    m_logOutput->append("Supported platforms: Vimeo, YouTube");
    m_logOutput->append("===========================");
#else
    m_logOutput->append("=== System Information ===");
    m_logOutput->append("Operating System: Linux/Other");
    m_logOutput->append("Tools installation: Manual installation required");
    m_logOutput->append("Supported platforms: Vimeo, YouTube");
    m_logOutput->append("===========================");
#endif
}

// provisional: lo reemplaza el rediseño
void MainWindow::refreshUpdateLink()
{
    if (!m_updateLinkButton || !m_updateService) {
        return;
    }
    const QString version = m_updateService->currentVersion();
    QString text;
    switch (m_updateService->state()) {
    case UpdateService::State::Checking:
        text = QString("v%1 · Checking for updates...").arg(version);
        break;
    case UpdateService::State::UpdateAvailable:
        text = QString("v%1 · Update to %2").arg(version, m_updateService->availableVersion());
        break;
    case UpdateService::State::Downloading:
        text = QString("v%1 · Downloading update...").arg(version);
        break;
    case UpdateService::State::Installing:
        text = QString("v%1 · Installing update...").arg(version);
        break;
    case UpdateService::State::InstallFailed:
        text = QString("v%1 · Update failed, check again").arg(version);
        break;
    case UpdateService::State::CheckFailed:
        text = QString("v%1 · Update check failed, retry").arg(version);
        break;
    case UpdateService::State::UpToDate:
        text = QString("v%1 · Up to date").arg(version);
        break;
    case UpdateService::State::Idle:
        text = QString("v%1 · Check for updates").arg(version);
        break;
    }
    m_updateLinkButton->setText(text);

    const QMap<QString, QString> tools = m_toolsManager ? m_toolsManager->toolVersions() : QMap<QString, QString>();
    auto toolText = [&tools](const QString &key) {
        const QString value = tools.value(key);
        return value.isEmpty() ? QString("not found") : value;
    };
    QString tip = QString("LGA Video Downloader %1\nyt-dlp %2\ndeno %3\nffmpeg %4\nDownloads powered by yt-dlp (github.com/yt-dlp/yt-dlp)")
                      .arg(version, toolText("yt-dlp"), toolText("deno"), toolText("ffmpeg"));
    if (!m_updateService->errorString().isEmpty()) {
        tip += "\n\n" + m_updateService->errorString();
    }
    const QString blocked = m_updateService->installBlockedReason();
    if (!blocked.isEmpty()) {
        tip += "\n" + blocked;
    }
    m_updateLinkButton->setToolTip(tip);
}

// provisional: lo reemplaza el rediseño
void MainWindow::onUpdateLinkClicked()
{
    const UpdateService::State state = m_updateService->state();
    if (state == UpdateService::State::Checking || state == UpdateService::State::Downloading
        || state == UpdateService::State::Installing) {
        return;
    }
    if (state != UpdateService::State::UpdateAvailable) {
        m_updateService->checkForUpdates();
        return;
    }
    const int active = m_downloadQueue ? m_downloadQueue->activeDownloadCount() : 0;
    if (active > 0 && UpdateService::installsInPlace()) {
        const auto answer = QMessageBox::question(this, "Update",
            QString("Updating will stop %1 active download(s). Continue?").arg(active),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    m_updateService->installAppUpdate();
    if (m_updateService->state() == UpdateService::State::InstallFailed) {
        QMessageBox::warning(this, "Update", m_updateService->errorString());
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_logGroup && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            onLogToggleClicked();
            return true;
        }
    }

    if (obj == m_settingsGroup && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            onSettingsToggleClicked();
            return true;
        }
    }

    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onLogToggleClicked()
{
    m_logExpanded = !m_logExpanded;

    if (m_logExpanded) {
        m_logGroup->setTitle("Log ⌄");
        m_logGroup->setProperty("collapsed", false); // Not collapsed
        m_logGroup->setFixedHeight(QWIDGETSIZE_MAX); // Permitir que se expanda
        m_logGroup->setMinimumHeight(0); // Sin altura mínima
        m_logGroup->setMaximumHeight(QWIDGETSIZE_MAX); // Sin límite máximo
        m_logLayout->setContentsMargins(3, 2, 3, 3); // Márgenes consistentes con CSS
        m_logLayout->setSpacing(2); // Espaciado pequeño entre widgets
        m_logOutput->show();
    } else {
        m_logGroup->setTitle("Log >");
        m_logGroup->setProperty("collapsed", true); // Collapsed
        m_logGroup->setFixedHeight(35); // Altura aumentada en 10px más
        m_logLayout->setContentsMargins(0, 0, 0, 0); // Sin márgenes cuando colapsado
        m_logLayout->setSpacing(0); // No spacing between widgets
        m_logOutput->hide();
    }

    // Force style refresh to apply new property
    m_logGroup->style()->unpolish(m_logGroup);
    m_logGroup->style()->polish(m_logGroup);

    // Adjust window size after toggling with a small delay to ensure proper layout calculation
    QTimer::singleShot(100, this, &MainWindow::adjustWindowSize);
}

void MainWindow::onSettingsToggleClicked()
{
    m_settingsExpanded = !m_settingsExpanded;

    if (m_settingsExpanded) {
        m_settingsGroup->setTitle("Settings ⌄");
        m_settingsGroup->setProperty("collapsed", false); // Not collapsed
        m_settingsGroup->setFixedHeight(QWIDGETSIZE_MAX); // Permitir que se expanda
        m_settingsGroup->setMinimumHeight(0); // Sin altura mínima
        m_settingsGroup->setMaximumHeight(QWIDGETSIZE_MAX); // Sin límite máximo
        // Mantener ancho consistente cuando está expandido también
        m_settingsGroup->setMinimumWidth(SETTINGS_GROUP_WIDTH);
        m_settingsGroup->setMaximumWidth(SETTINGS_GROUP_WIDTH);
        m_settingsLayout->setContentsMargins(3, 2, 3, 3); // Márgenes consistentes con CSS
        m_settingsLayout->setSpacing(8); // Espaciado normal
        // Show all settings widgets
        m_cookiesLabel->show();
        m_cookiesSourceCombo->show();
        m_downloadFolderInput->show();
        m_browseFolderButton->show();
        m_toolsButton->show();
        m_updateLinkButton->show(); // provisional: lo reemplaza el rediseño
    } else {
        m_settingsGroup->setTitle("Settings >");
        m_settingsGroup->setProperty("collapsed", true); // Collapsed
        m_settingsGroup->setFixedHeight(35); // Altura compacta
        // Usar ancho fijo consistente para mantener el layout estable
        m_settingsGroup->setMinimumWidth(SETTINGS_GROUP_WIDTH);
        m_settingsGroup->setMaximumWidth(SETTINGS_GROUP_WIDTH);
        m_settingsLayout->setContentsMargins(0, 0, 0, 0); // Sin márgenes cuando colapsado
        m_settingsLayout->setSpacing(0); // No spacing between widgets
        // Hide all settings widgets
        m_cookiesLabel->hide();
        m_cookiesSourceCombo->hide();
        m_downloadFolderInput->hide();
        m_browseFolderButton->hide();
        m_toolsButton->hide();
        m_updateLinkButton->hide(); // provisional: lo reemplaza el rediseño
    }

    // Force style refresh to apply new property
    m_settingsGroup->style()->unpolish(m_settingsGroup);
    m_settingsGroup->style()->polish(m_settingsGroup);

    // Adjust window size after toggling with a small delay to ensure proper layout calculation
    QTimer::singleShot(100, this, &MainWindow::adjustWindowSize);
}

// Height constants for window sizing (configurable values) - defined here for easy adjustment

void MainWindow::adjustWindowSize()
{
    // Height constants for window sizing (configurable values) - defined here for easy adjustment
    static const int MIN_HEIGHT_BOTH_EXPANDED = 600;      // Ambas secciones expandidas
    static const int MIN_HEIGHT_BOTH_COLLAPSED = 300;    // Ambas secciones contraídas
    static const int MIN_HEIGHT_LOG_EXPANDED = 500;      // Solo Log expandido, Settings contraído
    static const int MIN_HEIGHT_SETTINGS_EXPANDED = 350; // Solo Settings expandido, Log contraído
    static const int MAX_HEIGHT_LOG_EXPANDED = 650;      // Máximo cuando Log está expandido
    static const int MAX_HEIGHT_SETTINGS_EXPANDED = 620;  // Máximo cuando Settings está expandido

    // Forzar el cálculo del tamaño de todos los widgets
    m_centralWidget->adjustSize();

    // Obtener el tamaño sugerido por el layout
    QSize sizeHint = m_centralWidget->sizeHint();

    // El layout tiene restricción fija, así que establecemos el tamaño manualmente
    int extraWidth = 5;
    int currentWidth = sizeHint.width() + extraWidth;

    // Siempre mantener el ancho máximo registrado, independientemente del estado de expansión
    if (currentWidth > m_maxWindowWidth) {
        m_maxWindowWidth = currentWidth;
    }

    // Siempre usar el ancho máximo para evitar que las secciones se achiquen
    int finalWidth = m_maxWindowWidth > 0 ? m_maxWindowWidth : currentWidth;

    // Calcular altura según el estado del log y settings
    int finalHeight;

    // Usar el sizeHint cuando ambas secciones están en el mismo estado (ambas expandidas o ambas contraídas)
    bool anyExpanded = m_logExpanded || m_settingsExpanded;
    bool bothSameState = (m_logExpanded && m_settingsExpanded) || (!m_logExpanded && !m_settingsExpanded);

    if (bothSameState) {
        // Cuando ambas están en el mismo estado, usar el sizeHint que funciona bien
        finalHeight = sizeHint.height();
        finalHeight = anyExpanded ? qMax(MIN_HEIGHT_BOTH_EXPANDED, finalHeight) : qMax(MIN_HEIGHT_BOTH_COLLAPSED, finalHeight);
    } else {
        // Una sección expandida y otra contraída - usar constantes específicas
        if (m_logExpanded && !m_settingsExpanded) {
            // Solo Log expandido, Settings contraído
            finalHeight = sizeHint.height();
            if (finalHeight > MAX_HEIGHT_LOG_EXPANDED) {
                finalHeight = MAX_HEIGHT_LOG_EXPANDED;
            }
            finalHeight = qMax(MIN_HEIGHT_LOG_EXPANDED, finalHeight);
        } else if (!m_logExpanded && m_settingsExpanded) {
            // Solo Settings expandido, Log contraído
            finalHeight = sizeHint.height();
            if (finalHeight > MAX_HEIGHT_SETTINGS_EXPANDED) {
                finalHeight = MAX_HEIGHT_SETTINGS_EXPANDED;
            }
            finalHeight = qMax(MIN_HEIGHT_SETTINGS_EXPANDED, finalHeight);
        } else {
            // Fallback - ambas contraídas (aunque no debería llegar aquí)
            finalHeight = sizeHint.height();
            finalHeight = qMax(MIN_HEIGHT_BOTH_COLLAPSED, finalHeight);
        }
    }

    // Ajustar el tamaño de la ventana al tamaño óptimo
    setFixedSize(finalWidth, finalHeight);

    // También establecer el tamaño mínimo para permitir algo de flexibilidad
    setMinimumSize(400, finalHeight);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
}

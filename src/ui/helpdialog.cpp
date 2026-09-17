#include "videodownloader/helpdialog.h"
#include "videodownloader/theme.h"
#include "videodownloader/uiwidgets.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int DIALOG_WIDTH = 520;
constexpr int KEY_COLUMN = 96;

QString link(const QString &url, const QString &text)
{
    return QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration:none;\">%3</a>")
        .arg(url, QLatin1String(Theme::kLink), text.toHtmlEscaped());
}

QLabel *label(const QString &text, const char *name, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QLatin1String(name));
    return l;
}

QLabel *linkLabel(const QString &url, const QString &text, QWidget *parent)
{
    auto *l = label(link(url, text), "kvValue", parent);
    l->setTextFormat(Qt::RichText);
    l->setOpenExternalLinks(true);
    l->setTextInteractionFlags(Qt::TextBrowserInteraction);
    return l;
}

QString mib(qint64 bytes)
{
    return QString::number(bytes / (1024.0 * 1024.0), 'f', 1);
}

QHBoxLayout *keyValueRow(const QString &key, const char *keyName)
{
    auto *row = new QHBoxLayout();
    row->setSpacing(12);
    auto *keyLabel = new QLabel(key);
    keyLabel->setObjectName(QLatin1String(keyName));
    keyLabel->setFixedWidth(KEY_COLUMN);
    row->addWidget(keyLabel);
    return row;
}

} // namespace

// ------------------------------------------------------------------ Scrim

Scrim::Scrim(QWidget *parent)
    : QWidget(parent)
{
    setGeometry(parent->rect());
    parent->installEventFilter(this);
}

void Scrim::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(8, 8, 8, 184));
}

bool Scrim::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parent() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
    }
    return QWidget::eventFilter(watched, event);
}

// ------------------------------------------------------------------ HelpDialog

HelpDialog::HelpDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("helpDialog"));
    setWindowTitle(QStringLiteral("Help"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(DIALOG_WIDTH);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(12);

    // Titulo
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);
    titleRow->addWidget(label(QStringLiteral("LGA Video Downloader"), "helpTitle", this), 0, Qt::AlignBaseline);
    titleRow->addWidget(label(QStringLiteral("v" VIDEODOWNLOADER_VERSION), "helpVersion", this), 0, Qt::AlignBaseline);
    titleRow->addStretch(1);
    auto *close = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), this);
    Ui::setIcon(close, Icon::X, Theme::color(Theme::kIcon));
    close->setToolTip(QStringLiteral("Close"));
    titleRow->addWidget(close, 0, Qt::AlignVCenter);
    layout->addLayout(titleRow);

    // Caja del update: solo cuando hay algo que hacer (disponible, bajando, instalando, fallo).
    m_updateBox = new QFrame(this);
    m_updateBox->setObjectName(QStringLiteral("updateBox"));
    auto *updateLayout = new QHBoxLayout(m_updateBox);
    updateLayout->setContentsMargins(12, 11, 12, 11);
    updateLayout->setSpacing(12);
    m_dot = new QLabel(m_updateBox);
    m_dot->setFixedSize(8, 8);
    updateLayout->addWidget(m_dot, 0, Qt::AlignVCenter);
    auto *texts = new QVBoxLayout();
    texts->setSpacing(2);
    m_updateTitle = label(QString(), "updateTitle", m_updateBox);
    m_updateCaption = label(QString(), "caption", m_updateBox);
    m_updateCaption->setWordWrap(true);
    m_updateProgress = new QProgressBar(m_updateBox);
    m_updateProgress->setObjectName(QStringLiteral("updateProgress"));
    m_updateProgress->setTextVisible(false);
    m_updateProgress->setRange(0, 1000);
    texts->addWidget(m_updateTitle);
    texts->addWidget(m_updateProgress);
    texts->addWidget(m_updateCaption);
    updateLayout->addLayout(texts, 1);
    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_later = Ui::button(QStringLiteral("Later"), QStringLiteral("ghost"), QStringLiteral("sm"), m_updateBox);
    m_install = Ui::button(QStringLiteral("Update"), QStringLiteral("primary"), QStringLiteral("sm"), m_updateBox);
    m_cancelInstall = Ui::button(QStringLiteral("Cancel"), QString(), QStringLiteral("sm"), m_updateBox);
    buttons->addWidget(m_later);
    buttons->addWidget(m_install);
    buttons->addWidget(m_cancelInstall);
    updateLayout->addLayout(buttons);
    layout->addWidget(m_updateBox);

    // Fila "Updates": ultimo chequeo y boton para chequear
    auto *updatesRow = keyValueRow(QStringLiteral("Updates"), "kvKey");
    m_checkStatus = label(QString(), "kvValue", this);
    m_checkStatus->setWordWrap(true);
    m_check = Ui::button(QStringLiteral("Check now"), QString(), QStringLiteral("sm"), this);
    Ui::setIcon(m_check, Icon::Retry, Theme::color(Theme::kText));
    updatesRow->addWidget(m_checkStatus, 1);
    updatesRow->addWidget(m_check, 0, Qt::AlignVCenter);
    layout->addLayout(updatesRow);

    auto *authorRow = keyValueRow(QStringLiteral("Author"), "kvKey");
    auto *author = label(QStringLiteral("Lega Pugliese · ") + link(QStringLiteral("https://github.com/legandrop"),
                                                                 QStringLiteral("github.com/legandrop")), "kvValue", this);
    author->setTextFormat(Qt::RichText);
    author->setOpenExternalLinks(true);
    authorRow->addWidget(author, 1);
    layout->addLayout(authorRow);

    auto *rule = new QFrame(this);
    rule->setObjectName(QStringLiteral("helpRule"));
    layout->addWidget(rule);

    // Extension de navegador: como cargarla (sin Web Store, carpeta junto a la app).
    const auto strong = [](const QString &text) {
        return QStringLiteral("<span style=\"color:#F2F2F4;\">%1</span>").arg(text.toHtmlEscaped());
    };
    auto *extensionRow = new QHBoxLayout();
    extensionRow->setSpacing(12);
    extensionRow->addWidget(label(QStringLiteral("Browser extension"), "kvKeyStrong", this), 0, Qt::AlignVCenter);
    extensionRow->addStretch(1);
    auto *openExtension = Ui::button(QStringLiteral("Open extension folder"), QString(), QStringLiteral("sm"), this);
    extensionRow->addWidget(openExtension, 0, Qt::AlignVCenter);
    layout->addLayout(extensionRow);
    auto *extensionIntro = label(QStringLiteral("Send the page you're on to the queue with one click, using your "
                                                "browser session."), "helpBody", this);
    extensionIntro->setWordWrap(true);
    layout->addWidget(extensionIntro);
    const QStringList steps = {
        QStringLiteral("Open %1 (or %2, %3).").arg(strong(QStringLiteral("brave://extensions")),
                                                   strong(QStringLiteral("chrome://extensions")),
                                                   strong(QStringLiteral("edge://extensions"))),
        QStringLiteral("Turn on %1 and keep it on: turning it off disables the extension.")
            .arg(strong(QStringLiteral("Developer mode"))),
        QStringLiteral("Click %1 and pick the extension folder.").arg(strong(QStringLiteral("Load unpacked"))),
        QStringLiteral("Pin LGA Video Downloader in the toolbar."),
    };
    auto *stepsLayout = new QVBoxLayout();
    stepsLayout->setSpacing(4);
    for (int i = 0; i < steps.size(); ++i) {
        auto *row = new QHBoxLayout();
        row->setSpacing(8);
        auto *number = label(QString::number(i + 1), "kvKey", this);
        number->setFixedWidth(12);
        row->addWidget(number, 0, Qt::AlignTop);
        auto *text = label(steps.at(i), "helpBody", this);
        text->setTextFormat(Qt::RichText);
        text->setWordWrap(true);
        row->addWidget(text, 1);
        stepsLayout->addLayout(row);
    }
    layout->addLayout(stepsLayout);

    auto *extensionRule = new QFrame(this);
    extensionRule->setObjectName(QStringLiteral("helpRule"));
    layout->addWidget(extensionRule);

    // Creditos: la app es una interfaz; el trabajo lo hacen estas herramientas.
    auto *body = label(QStringLiteral("This app is a <span style=\"color:#F2F2F4;\">download queue for yt-dlp</span>. "
                                      "yt-dlp does the downloading; FFmpeg and Deno support it. Credit for site "
                                      "support belongs to those projects."), "helpBody", this);
    body->setTextFormat(Qt::RichText);
    body->setWordWrap(true);
    layout->addWidget(body);

    struct Credit { const char *name; const char *role; const char *url; const char *shown; QLabel **version; };
    const Credit credits[] = {
        {"yt-dlp", "Download engine", "https://github.com/yt-dlp/yt-dlp", "github.com/yt-dlp/yt-dlp", &m_ytdlpVersion},
        {"FFmpeg", "Merges audio and video", "https://ffmpeg.org", "ffmpeg.org", &m_ffmpegVersion},
        {"Deno", "JS runtime for YouTube", "https://deno.com", "deno.com", &m_denoVersion},
    };
    for (const Credit &credit : credits) {
        auto *row = keyValueRow(QString::fromLatin1(credit.name), "kvKeyStrong");
        row->addWidget(label(QString::fromLatin1(credit.role), "kvValue", this));
        *credit.version = label(QString(), "caption", this);
        row->addWidget(*credit.version, 0, Qt::AlignVCenter);
        row->addStretch(1);
        row->addWidget(linkLabel(QString::fromLatin1(credit.url), QString::fromLatin1(credit.shown), this));
        layout->addLayout(row);
    }
    layout->addWidget(label(QStringLiteral("Each tool is distributed under its own license."), "caption", this));

    auto *footer = new QHBoxLayout();
    footer->setContentsMargins(0, 4, 0, 0);
    footer->addStretch(1);
    auto *closeButton = Ui::button(QStringLiteral("Close"), QString(), QString(), this);
    closeButton->setObjectName(QStringLiteral("closeButton"));
    closeButton->setDefault(true);
    // Marca del boton de Enter (DialogStyle LGA): borde violeta y resplandor suave.
    auto *glow = new QGraphicsDropShadowEffect(closeButton);
    glow->setColor(QColor(0x4C, 0x30, 0x78, 140));
    glow->setBlurRadius(16);
    glow->setOffset(0, 0);
    closeButton->setGraphicsEffect(glow);
    footer->addWidget(closeButton);
    layout->addLayout(footer);

    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_later, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_check, &QPushButton::clicked, this, &HelpDialog::checkRequested);
    connect(m_install, &QPushButton::clicked, this, &HelpDialog::installRequested);
    connect(m_cancelInstall, &QPushButton::clicked, this, &HelpDialog::cancelInstallRequested);
    connect(openExtension, &QPushButton::clicked, this, &HelpDialog::openExtensionFolderRequested);

    setToolVersions({});
    setUpdateView(UpdateView());
}

void HelpDialog::setToolVersions(const QMap<QString, QString> &versions)
{
    const auto shown = [&versions](const char *key) {
        const QString value = versions.value(QLatin1String(key));
        return value.isEmpty() ? QStringLiteral("not found") : value;
    };
    m_ytdlpVersion->setText(shown("yt-dlp"));
    m_ffmpegVersion->setText(shown("ffmpeg"));
    m_denoVersion->setText(shown("deno"));
}

void HelpDialog::setUpdateView(const UpdateView &view)
{
    using State = UpdateService::State;
    QString dot = QLatin1String(Theme::kAccent);
    QString title;
    QString caption;
    bool box = false;
    bool accent = true;
    bool showInstall = false;
    bool showLater = false;
    bool showCancel = false;
    bool showProgress = false;
    QString installText = QStringLiteral("Update");

    const QString checked = view.lastChecked.isValid()
        ? (view.lastChecked.date() == QDate::currentDate()
               ? QStringLiteral("checked today at %1").arg(view.lastChecked.toString(QStringLiteral("HH:mm")))
               : QStringLiteral("checked on %1").arg(view.lastChecked.toString(QStringLiteral("yyyy-MM-dd HH:mm"))))
        : QString();
    const auto capitalized = [](QString text) {
        if (!text.isEmpty()) {
            text[0] = text.at(0).toUpper();
        }
        return text;
    };
    QString status = checked.isEmpty() ? QStringLiteral("Not checked yet") : capitalized(checked);
    bool canCheck = true;

    switch (view.state) {
    case State::Idle:
        break;
    case State::Checking:
        status = QStringLiteral("Checking for updates…");
        canCheck = false;
        break;
    case State::UpToDate:
        status = checked.isEmpty() ? QStringLiteral("Up to date") : QStringLiteral("Up to date · %1").arg(checked);
        break;
    case State::CheckFailed:
        status = QStringLiteral("Couldn't check: %1").arg(view.error);
        break;
    case State::UpdateAvailable:
        box = true;
        title = QStringLiteral("Update available · v%1").arg(view.availableVersion);
        caption = !view.blockedReason.isEmpty() ? view.blockedReason
                  : view.installsInPlace ? QStringLiteral("Closes the app, installs and opens it again")
                                         : QStringLiteral("Opens the download page");
        showLater = true;
        showInstall = true;
        break;
    case State::Downloading:
        box = true;
        title = QStringLiteral("Downloading update · v%1").arg(view.availableVersion);
        showProgress = true;
        caption = view.total > 0 ? QStringLiteral("%1 of %2 MiB").arg(mib(view.received), mib(view.total))
                                 : QStringLiteral("Starting download…");
        showCancel = true;
        canCheck = false;
        break;
    case State::Installing:
        box = true;
        title = QStringLiteral("Installing update…");
        caption = QStringLiteral("The app will close and open again by itself");
        canCheck = false;
        break;
    case State::InstallFailed:
        box = true;
        accent = false;
        dot = QLatin1String(Theme::kError);
        title = QStringLiteral("The update failed");
        caption = view.error;
        showInstall = true;
        installText = QStringLiteral("Try again");
        break;
    }

    m_updateBox->setVisible(box);
    m_dot->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 4px;").arg(dot));
    m_updateTitle->setText(title);
    m_updateCaption->setText(caption);
    m_updateCaption->setVisible(!caption.isEmpty());
    m_updateProgress->setVisible(showProgress);
    if (showProgress) {
        m_updateProgress->setValue(view.total > 0 ? int(view.received * 1000 / view.total) : 0);
    }
    m_install->setText(installText);
    m_install->setVisible(showInstall);
    m_install->setEnabled(view.blockedReason.isEmpty());
    m_later->setVisible(showLater);
    m_cancelInstall->setVisible(showCancel);
    m_checkStatus->setText(status);
    m_check->setEnabled(canCheck);
    if (m_updateBox->property("accent").toBool() != accent) {
        m_updateBox->setProperty("accent", accent);
        Ui::repolish(m_updateBox);
    }
    adjustSize();
}

int HelpDialog::execOver(QWidget *window)
{
    auto *scrim = new Scrim(window);
    scrim->show();
    adjustSize();
    const QPoint center = window->mapToGlobal(window->rect().center());
    move(center - QPoint(width() / 2, height() / 2));
    const int result = exec();
    delete scrim;
    return result;
}

void HelpDialog::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Theme::color(Theme::kBorder), 1.0));
    painter.setBrush(Theme::color(Theme::kDialog));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}

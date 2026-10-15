#include "videodownloader/helpdialog.h"
#include "videodownloader/theme.h"
#include "videodownloader/uiwidgets.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {

constexpr int DIALOG_WIDTH = 520;
constexpr int KEY_COLUMN = 96;

// Los links del Help van en #8C7ED9 sin subrayar: el mismo link a lega.com.ar en todas las apps.
constexpr const char *HELP_LINK_COLOR = "#8C7ED9";

QString link(const QString &url, const QString &text)
{
    return QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration:none;\">%3</a>")
        .arg(url, QLatin1String(HELP_LINK_COLOR), text.toHtmlEscaped());
}

// Version de una tool tal como entra en la fila de creditos. El ancho de la fila es fijo
// (DIALOG_WIDTH) y la version comparte el lugar con el rol: con un build nocturno de ffmpeg
// ("N-117208-gbd22d7e601-20240927", 203 px) se comia el rol y los dos quedaban cortados. Los
// nocturnos se muestran por su fecha y cualquier otra cadena larga se recorta; la completa va
// en el tooltip.
QString shortToolVersion(const QString &version)
{
    static const QRegularExpression nightly(QStringLiteral("^N-\\d+-g[0-9a-f]+-(\\d{4})(\\d{2})(\\d{2})$"));
    const QRegularExpressionMatch match = nightly.match(version);
    if (match.hasMatch()) {
        return QStringLiteral("%1-%2-%3").arg(match.captured(1), match.captured(2), match.captured(3));
    }
    constexpr int kMaxChars = 16;
    if (version.size() > kMaxChars) {
        return version.left(kMaxChars - 1) + QChar(0x2026);
    }
    return version;
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
    titleRow->addSpacing(6);
    m_whatsNewLink = label(link(QStringLiteral("#whats-new"), QStringLiteral("What's new")), "whatsNewLink", this);
    m_whatsNewLink->setTextFormat(Qt::RichText);
    titleRow->addWidget(m_whatsNewLink, 0, Qt::AlignBaseline);
    titleRow->addStretch(1);
    auto *close = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), this);
    Ui::setIcon(close, Icon::X, Theme::color(Theme::kIcon));
    close->setToolTip(QStringLiteral("Close"));
    titleRow->addWidget(close, 0, Qt::AlignVCenter);
    layout->addLayout(titleRow);

    // Caja del update: solo cuando un chequeo encontro una version nueva. La descarga y la
    // instalacion siguen en los carteles de lgaupdate.
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
    texts->addWidget(m_updateTitle);
    texts->addWidget(m_updateCaption);
    updateLayout->addLayout(texts, 1);
    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_later = Ui::button(QStringLiteral("Later"), QStringLiteral("ghost"), QStringLiteral("sm"), m_updateBox);
    m_install = Ui::button(QStringLiteral("Update"), QStringLiteral("primary"), QStringLiteral("sm"), m_updateBox);
    buttons->addWidget(m_later);
    buttons->addWidget(m_install);
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
    auto *author = label(QStringLiteral("Lega Pugliese · ") + link(QStringLiteral("https://lega.com.ar"),
                                                                 QStringLiteral("lega.com.ar")), "kvValue", this);
    author->setTextFormat(Qt::RichText);
    author->setOpenExternalLinks(true);
    authorRow->addWidget(author, 1);
    layout->addLayout(authorRow);

    // Extension y creditos.
    auto *detailsWidget = new QWidget(this);
    auto *details = new QVBoxLayout(detailsWidget);
    details->setContentsMargins(0, 0, 0, 0);
    details->setSpacing(12);

    auto *rule = new QFrame(this);
    rule->setObjectName(QStringLiteral("helpRule"));
    details->addWidget(rule);

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
    details->addLayout(extensionRow);
    auto *extensionIntro = label(QStringLiteral("Send the page you're on to the queue with one click, using your "
                                                "browser session."), "helpBody", this);
    extensionIntro->setWordWrap(true);
    details->addWidget(extensionIntro);
    const QStringList steps = {
        QStringLiteral("Open %1 (Chrome: %2).").arg(strong(QStringLiteral("brave://extensions")),
                                                    strong(QStringLiteral("chrome://extensions"))),
        QStringLiteral("Turn on %1. Keep it on, or the extension stops.").arg(strong(QStringLiteral("Developer mode"))),
        QStringLiteral("Click %1 and pick the extension folder.").arg(strong(QStringLiteral("Load unpacked"))),
        QStringLiteral("Pin LGA Video Downloader in the toolbar."),
    };
    // Un QLabel de UNA linea por paso, sin wordWrap. Con wordWrap (heightForWidth) la ventana real
    // les daba menos alto que el texto y quedaban de 9 px, encimados; el render offscreen de
    // --ui-shot no lo reproducia. Por eso los textos son cortos: cada uno entra en el ancho fijo.
    auto *stepsLayout = new QVBoxLayout();
    stepsLayout->setSpacing(4);
    for (int i = 0; i < steps.size(); ++i) {
        auto *text = label(QStringLiteral("%1.&nbsp;&nbsp;%2").arg(i + 1).arg(steps.at(i)), "helpBody", this);
        text->setTextFormat(Qt::RichText);
        stepsLayout->addWidget(text);
    }
    details->addLayout(stepsLayout);

    auto *extensionRule = new QFrame(this);
    extensionRule->setObjectName(QStringLiteral("helpRule"));
    details->addWidget(extensionRule);

    // Creditos: la app es una interfaz; el trabajo lo hacen estas herramientas.
    auto *body = label(QStringLiteral("This app is a <span style=\"color:#F2F2F4;\">download queue for yt-dlp</span>. "
                                      "yt-dlp does the downloading; FFmpeg and Deno support it. Credit for site "
                                      "support belongs to those projects."), "helpBody", this);
    body->setTextFormat(Qt::RichText);
    body->setWordWrap(true);
    details->addWidget(body);

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
        details->addLayout(row);
    }
    details->addWidget(label(QStringLiteral("Each tool is distributed under its own license."), "caption", this));
    layout->addWidget(detailsWidget);

    auto *footer = new QHBoxLayout();
    footer->setContentsMargins(0, 4, 0, 0);
    footer->addStretch(1);
    auto *closeButton = Ui::button(QStringLiteral("Close"), QString(), QString(), this);
    closeButton->setObjectName(QStringLiteral("closeButton"));
    m_closeButton = closeButton;
    footer->addWidget(closeButton);
    layout->addLayout(footer);

    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_later, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_check, &QPushButton::clicked, this, &HelpDialog::checkRequested);
    connect(m_install, &QPushButton::clicked, this, &HelpDialog::installRequested);
    connect(openExtension, &QPushButton::clicked, this, &HelpDialog::openExtensionFolderRequested);
    connect(m_whatsNewLink, &QLabel::linkActivated, this, &HelpDialog::whatsNewRequested);

    setToolVersions({});
    setUpdateView(UpdateView());
}

void HelpDialog::setToolVersions(const QMap<QString, QString> &versions)
{
    const auto show = [&versions](QLabel *target, const char *key) {
        const QString value = versions.value(QLatin1String(key));
        if (value.isEmpty()) {
            target->setText(QStringLiteral("not found"));
            target->setToolTip(QString());
            return;
        }
        const QString shown = shortToolVersion(value);
        target->setText(shown);
        target->setToolTip(shown == value ? QString() : value);
    };
    show(m_ytdlpVersion, "yt-dlp");
    show(m_ffmpegVersion, "ffmpeg");
    show(m_denoVersion, "deno");
}

void HelpDialog::setUpdateView(const UpdateView &view)
{
    // Sin hora del ultimo chequeo: lgaupdate no informa si termino bien o fallo, y "Last checked"
    // despues de un chequeo sin red diria algo que no paso. El resultado de un Check now lo
    // muestra su cartel.
    QString status = QStringLiteral("Checked automatically every few hours");
    if (!view.blockedReason.isEmpty()) {
        status = view.blockedReason;
    } else if (view.checking) {
        status = QStringLiteral("Checking for updates…");
    }

    const bool box = !view.availableVersion.isEmpty();
    m_updateBox->setVisible(box);
    m_dot->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 4px;").arg(QLatin1String(Theme::kAccent)));
    m_updateTitle->setText(QStringLiteral("Update available · v%1").arg(view.availableVersion));
#ifdef Q_OS_WIN
    // En Windows lgaupdate abre el wizard del instalador (no lo corre en silencio).
    const QString installCaption = QStringLiteral("Closes the app and opens the installer");
#else
    const QString installCaption = QStringLiteral("Closes the app, installs and opens it again");
#endif
    m_updateCaption->setText(!view.blockedReason.isEmpty() ? view.blockedReason : installCaption);
    m_install->setVisible(box);
    m_install->setEnabled(view.blockedReason.isEmpty() && !view.checking);
    // Enter ejecuta la accion principal y ese boton va siempre marcado: Update si se puede
    // instalar; si no, Close.
    const bool installIsEnter = box && m_install->isEnabled();
    Ui::setEnterButton(m_install, installIsEnter);
    Ui::setEnterButton(m_closeButton, !installIsEnter);
    m_later->setVisible(box);
    m_checkStatus->setText(status);
    m_check->setEnabled(view.blockedReason.isEmpty() && !view.checking);
    if (!m_updateBox->property("accent").toBool()) {
        m_updateBox->setProperty("accent", true);
        Ui::repolish(m_updateBox);
    }
    fitHeight();
}

void HelpDialog::fitHeight()
{
    // adjustSize() calculaba el alto con los labels todavia sin la fuente de la hoja de estilo y
    // en la ventana real quedaba corto: el layout comprimia los pasos de la extension a 9 px.
    // Se pule todo primero y se toma el alto que pide el layout para el ancho fijo.
    ensurePolished();
    for (QWidget *child : findChildren<QWidget *>()) {
        child->ensurePolished();
    }
    layout()->invalidate();
    layout()->activate();
    const int needed = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(DIALOG_WIDTH)
                                                     : layout()->totalSizeHint().height();
    setMinimumHeight(needed);
    resize(DIALOG_WIDTH, needed);
}

int HelpDialog::execOver(QWidget *window)
{
    auto *scrim = new Scrim(window);
    scrim->show();
    fitHeight();
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

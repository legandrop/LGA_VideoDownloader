#include "videodownloader/helpdialog.h"
#include "videodownloader/theme.h"
#include "videodownloader/uiwidgets.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int DIALOG_WIDTH = 520;
constexpr int KEY_COLUMN = 96;

// Los links del Help van en #8C7ED9 sin subrayar: el mismo link a lega.com.ar en todas las apps.
constexpr const char *HELP_LINK_COLOR = "#8C7ED9";

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

// ------------------------------------------------------------------ PanelDialog

PanelDialog::PanelDialog(const QString &objectName, const QString &windowTitle, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(objectName);
    setWindowTitle(windowTitle);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(DIALOG_WIDTH);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(12);

    m_titleRow = new QHBoxLayout();
    m_titleRow->setSpacing(8);
    layout->addLayout(m_titleRow);

    m_body = new QVBoxLayout();
    m_body->setSpacing(12);
    layout->addLayout(m_body);
}

void PanelDialog::finish()
{
    m_titleRow->addStretch(1);
    auto *close = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), this);
    Ui::setIcon(close, Icon::X, Theme::color(Theme::kIcon));
    close->setToolTip(QStringLiteral("Close"));
    m_titleRow->addWidget(close, 0, Qt::AlignVCenter);

    auto *footer = new QHBoxLayout();
    footer->setContentsMargins(0, 4, 0, 0);
    footer->addStretch(1);
    m_closeButton = Ui::button(QStringLiteral("Close"), QString(), QString(), this);
    m_closeButton->setObjectName(QStringLiteral("closeButton"));
    Ui::setEnterButton(m_closeButton, true);
    footer->addWidget(m_closeButton);
    static_cast<QVBoxLayout *>(layout())->addLayout(footer);

    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

QString PanelDialog::link(const QString &url, const QString &text)
{
    return QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration:none;\">%3</a>")
        .arg(url, QLatin1String(HELP_LINK_COLOR), text.toHtmlEscaped());
}

QLabel *PanelDialog::label(const QString &text, const char *name)
{
    auto *l = new QLabel(text, this);
    l->setObjectName(QLatin1String(name));
    return l;
}

QLabel *PanelDialog::linkLabel(const QString &url, const QString &text)
{
    auto *l = label(link(url, text), "kvValue");
    l->setTextFormat(Qt::RichText);
    l->setOpenExternalLinks(true);
    l->setTextInteractionFlags(Qt::TextBrowserInteraction);
    return l;
}

QHBoxLayout *PanelDialog::keyValueRow(const QString &key, const char *keyName)
{
    auto *row = new QHBoxLayout();
    row->setSpacing(12);
    auto *keyLabel = label(key, keyName);
    keyLabel->setFixedWidth(KEY_COLUMN);
    row->addWidget(keyLabel);
    return row;
}

void PanelDialog::addRule(QBoxLayout *layout)
{
    auto *rule = new QFrame(this);
    rule->setObjectName(QStringLiteral("helpRule"));
    layout->addWidget(rule);
}

void PanelDialog::fitHeight()
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
    // Abierto (la caja del update de Settings aparece o se va durante un chequeo), el alto cambia
    // alrededor del mismo centro: si no, el dialogo crecia hacia abajo y quedaba descentrado.
    const QPoint center = geometry().center();
    const bool keepCenter = isVisible() && height() != needed;
    setMinimumHeight(needed);
    resize(DIALOG_WIDTH, needed);
    if (keepCenter) {
        move(center - QPoint(width() / 2, height() / 2));
    }
}

int PanelDialog::execOver(QWidget *window)
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

void PanelDialog::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Theme::color(Theme::kBorder), 1.0));
    painter.setBrush(Theme::color(Theme::kDialog));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}

// ------------------------------------------------------------------ HelpDialog

HelpDialog::HelpDialog(QWidget *parent)
    : PanelDialog(QStringLiteral("helpDialog"), QStringLiteral("Help"), parent)
{
    titleRow()->addWidget(label(QStringLiteral("LGA Video Downloader"), "helpTitle"), 0, Qt::AlignBaseline);
    titleRow()->addWidget(label(QStringLiteral("v" VIDEODOWNLOADER_VERSION), "helpVersion"), 0, Qt::AlignBaseline);
    titleRow()->addSpacing(6);
    auto *whatsNew = label(link(QStringLiteral("#whats-new"), QStringLiteral("What's new")), "whatsNewLink");
    whatsNew->setTextFormat(Qt::RichText);
    titleRow()->addWidget(whatsNew, 0, Qt::AlignBaseline);

    auto *authorRow = keyValueRow(QStringLiteral("Author"), "kvKey");
    auto *author = label(QStringLiteral("Lega Pugliese · ") + link(QStringLiteral("https://lega.com.ar"),
                                                                 QStringLiteral("lega.com.ar")), "kvValue");
    author->setTextFormat(Qt::RichText);
    author->setOpenExternalLinks(true);
    authorRow->addWidget(author, 1);
    body()->addLayout(authorRow);

    addRule(body());

    // Creditos: la app es una interfaz; el trabajo lo hacen estas herramientas. Sus versiones
    // instaladas estan en Settings.
    auto *about = label(QStringLiteral("This app is a <span style=\"color:#F2F2F4;\">download queue for yt-dlp</span>. "
                                       "yt-dlp does the downloading; FFmpeg and Deno support it. Credit for site "
                                       "support belongs to those projects."), "helpBody");
    about->setTextFormat(Qt::RichText);
    about->setWordWrap(true);
    body()->addWidget(about);

    struct Credit { const char *name; const char *role; const char *url; const char *shown; };
    const Credit credits[] = {
        {"yt-dlp", "Download engine", "https://github.com/yt-dlp/yt-dlp", "github.com/yt-dlp/yt-dlp"},
        {"FFmpeg", "Merges audio and video", "https://ffmpeg.org", "ffmpeg.org"},
        {"Deno", "JS runtime for YouTube", "https://deno.com", "deno.com"},
    };
    for (const Credit &credit : credits) {
        auto *row = keyValueRow(QString::fromLatin1(credit.name), "kvKeyStrong");
        row->addWidget(label(QString::fromLatin1(credit.role), "kvValue"));
        row->addStretch(1);
        row->addWidget(linkLabel(QString::fromLatin1(credit.url), QString::fromLatin1(credit.shown)));
        body()->addLayout(row);
    }
    body()->addWidget(label(QStringLiteral("Each tool is distributed under its own license."), "caption"));

    finish();
    connect(whatsNew, &QLabel::linkActivated, this, &HelpDialog::whatsNewRequested);
    fitHeight();
}

#include "videodownloader/whatsnewdialog.h"
#include "videodownloader/helpdialog.h"
#include "videodownloader/theme.h"
#include "videodownloader/uiwidgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QScopedPointer>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int DIALOG_WIDTH = 520;
constexpr int MARGIN = 22;
constexpr int NOTES_MAX_HEIGHT = 380;
} // namespace

QTextBrowser *NotesView::create(QWidget *parent)
{
    auto *view = new QTextBrowser(parent);
    view->setObjectName(QStringLiteral("whatsNewNotes"));
    // Los links del texto (el de GitHub cuando no hay notas) abren el navegador.
    view->setOpenExternalLinks(true);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->document()->setDocumentMargin(10);
    return view;
}

void NotesView::setHtml(QTextBrowser *view, const QString &html, int width, int maxHeight)
{
    view->setHtml(html);
    view->ensurePolished();
    view->document()->setDefaultFont(view->font());
    // Se mide en una copia del documento: al del QTextBrowser lo vuelve a diagramar el propio
    // widget con el ancho de su viewport, que antes de mostrarse no es el real, y el bloque
    // quedaba mucho mas alto que el texto. Con el ancho que queda si aparece la barra: si no
    // aparece, sobra apenas un poco.
    const int frame = 2 * view->frameWidth();
    QScopedPointer<QTextDocument> measure(view->document()->clone());
    measure->setDefaultFont(view->font());
    measure->setDocumentMargin(view->document()->documentMargin());
    measure->setTextWidth(width - frame - view->verticalScrollBar()->sizeHint().width());
    const int needed = int(std::ceil(measure->size().height())) + frame;
    view->setFixedHeight(qMin(maxHeight, needed));
}

WhatsNewDialog::WhatsNewDialog(const QString &version, const QString &notesHtml, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("whatsNewDialog"));
    setWindowTitle(QStringLiteral("What's new"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(DIALOG_WIDTH);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(MARGIN, 20, MARGIN, 18);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("What's new"), this);
    title->setObjectName(QStringLiteral("helpTitle"));
    layout->addWidget(title);
    auto *intro = new QLabel(QStringLiteral("LGA Video Downloader was updated to v%1.").arg(version.toHtmlEscaped()), this);
    intro->setObjectName(QStringLiteral("helpBody"));
    layout->addWidget(intro);

    auto *notes = NotesView::create(this);
    NotesView::setHtml(notes, notesHtml, DIALOG_WIDTH - 2 * MARGIN, NOTES_MAX_HEIGHT);
    layout->addWidget(notes);

    auto *footer = new QHBoxLayout();
    footer->setContentsMargins(0, 4, 0, 0);
    footer->addStretch(1);
    auto *close = Ui::button(QStringLiteral("Close"), QString(), QString(), this);
    close->setObjectName(QStringLiteral("closeButton"));
    // Es la unica accion del dialogo, asi que es la de Enter.
    Ui::setEnterButton(close, true);
    footer->addWidget(close);
    layout->addLayout(footer);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
}

void WhatsNewDialog::fitHeight()
{
    ensurePolished();
    for (QWidget *child : findChildren<QWidget *>()) {
        child->ensurePolished();
    }
    layout()->invalidate();
    layout()->activate();
    const int needed = layout()->totalSizeHint().height();
    setMinimumHeight(needed);
    resize(DIALOG_WIDTH, needed);
}

int WhatsNewDialog::execOver(QWidget *window)
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

void WhatsNewDialog::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Theme::color(Theme::kBorder), 1.0));
    painter.setBrush(Theme::color(Theme::kDialog));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}

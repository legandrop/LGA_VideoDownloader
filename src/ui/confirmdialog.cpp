#include "videodownloader/ConfirmDialog.h"
#include "videodownloader/helpdialog.h"
#include "videodownloader/theme.h"
#include "videodownloader/uiwidgets.h"

#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopedPointer>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

#include <cmath>

namespace {
// Mismo ancho y margenes que Help: los dos carteles se ven como la misma familia.
constexpr int DIALOG_WIDTH = 520;
constexpr int MARGIN = 22;
constexpr int BUTTON_MIN_WIDTH = 120;
constexpr int NOTES_MAX_HEIGHT = 260;
constexpr int DETAILS_HEIGHT = 140;

// Las palabras en <b> resaltan en el blanco de los textos destacados de Help; las teclas de la
// fila de atajos, en violeta.
QString tintBold(QString html, const char *color)
{
    html.replace(QStringLiteral("<b>"), QStringLiteral("<span style=\"color:%1;\"><b>").arg(QLatin1String(color)));
    html.replace(QStringLiteral("</b>"), QStringLiteral("</b></span>"));
    return html;
}

// Notas con scroll propio: el alto del texto para el ancho del cartel, nunca mas que el tope.
// Se mide en una copia del documento, igual que el bloque de notas que tenia Help: el del
// QTextBrowser lo vuelve a diagramar el widget con el ancho de su viewport, que antes de
// mostrarse no es el real.
void fitNotes(QTextBrowser *view, int width)
{
    view->ensurePolished();
    view->document()->setDefaultFont(view->font());
    const int frame = 2 * view->frameWidth();
    QScopedPointer<QTextDocument> measure(view->document()->clone());
    measure->setDefaultFont(view->font());
    measure->setDocumentMargin(view->document()->documentMargin());
    measure->setTextWidth(width - frame - view->verticalScrollBar()->sizeHint().width());
    const int needed = int(std::ceil(measure->size().height())) + frame;
    view->setFixedHeight(qMin(NOTES_MAX_HEIGHT, needed));
}
} // namespace

ConfirmDialog::ConfirmDialog(const Spec &spec, QWidget *parent)
    : QDialog(parent)
    , m_result(spec.escapeIndex)
{
    setObjectName(QStringLiteral("confirmDialog"));
    setWindowTitle(spec.title);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    const int width = qMax(DIALOG_WIDTH, int(spec.buttons.size()) * (BUTTON_MIN_WIDTH + 8) + 2 * MARGIN);
    setFixedWidth(width);
    const int contentWidth = width - 2 * MARGIN;

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(MARGIN, 20, MARGIN, 18);
    layout->setSpacing(12);

    auto *title = new QLabel(spec.title, this);
    title->setObjectName(QStringLiteral("helpTitle"));
    title->setWordWrap(true);
    layout->addWidget(title);

    if (!spec.subtitle.isEmpty()) {
        auto *subtitle = new QLabel(tintBold(spec.subtitle, Theme::kTextEmphasis), this);
        subtitle->setObjectName(QStringLiteral("helpBody"));
        subtitle->setTextFormat(Qt::RichText);
        subtitle->setWordWrap(true);
        subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(subtitle);
    }

    if (!spec.contentHtml.isEmpty()) {
        auto *notes = new QTextBrowser(this);
        notes->setObjectName(QStringLiteral("whatsNewNotes"));
        // Los links de las notas no navegan dentro del cartel ni abren nada.
        notes->setOpenLinks(false);
        notes->setOpenExternalLinks(false);
        notes->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        notes->document()->setDocumentMargin(10);
        notes->setHtml(spec.contentHtml);
        fitNotes(notes, contentWidth);
        layout->addWidget(notes);
    }

    if (!spec.details.isEmpty()) {
        auto *toggle = Ui::button(QStringLiteral("Details"), QStringLiteral("ghost"), QStringLiteral("sm"), this);
        auto *details = new QPlainTextEdit(spec.details, this);
        details->setObjectName(QStringLiteral("confirmDetails"));
        details->setReadOnly(true);
        details->setFixedHeight(DETAILS_HEIGHT);
        details->hide();
        connect(toggle, &QPushButton::clicked, this, [this, toggle, details]() {
            const bool expanded = !details->isVisible();
            details->setVisible(expanded);
            toggle->setText(expanded ? QStringLiteral("Hide details") : QStringLiteral("Details"));
            fitHeight();
        });
        layout->addWidget(toggle, 0, Qt::AlignLeft);
        layout->addWidget(details);
    }

    if (!spec.hintHtml.isEmpty()) {
        auto *hint = new QLabel(tintBold(spec.hintHtml, Theme::kLink), this);
        hint->setObjectName(QStringLiteral("caption"));
        hint->setTextFormat(Qt::RichText);
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }

    auto *footer = new QHBoxLayout();
    footer->setContentsMargins(0, 4, 0, 0);
    footer->setSpacing(8);
    footer->addStretch(1);
    QPushButton *defaultButton = nullptr;
    for (int i = 0; i < spec.buttons.size(); ++i) {
        const Button &b = spec.buttons.at(i);
        const QString variant = b.style == ButtonStyle::Action ? QStringLiteral("primary")
                                : b.style == ButtonStyle::Danger ? QStringLiteral("danger")
                                                                 : QString();
        auto *btn = Ui::button(b.text, variant, QString(), this);
        btn->setObjectName(QStringLiteral("confirmButton"));
        // La tecla se asigna explicita: en macOS el "&" del texto no hace de atajo.
        if (b.shortcut != Qt::Key_unknown) {
            btn->setShortcut(QKeySequence(b.shortcut));
        }
        // Los destructivos quedan fuera del Tab: sin ver donde quedo el foco, Tab + Espacio
        // ejecutaria la accion.
        btn->setFocusPolicy(spec.noFocusIndexes.contains(i) ? Qt::NoFocus : Qt::StrongFocus);
        btn->setAutoDefault(false);
        const bool isDefault = i == spec.defaultIndex;
        Ui::setEnterButton(btn, isDefault);
        if (isDefault) {
            defaultButton = btn;
        }
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            m_result = i;
            accept();
        });
        footer->addWidget(btn);
    }
    layout->addLayout(footer);

    if (defaultButton) {
        defaultButton->setFocus();
    }
}

ConfirmDialog *ConfirmDialog::build(const Spec &spec, QWidget *parent)
{
    return new ConfirmDialog(spec, parent);
}

void ConfirmDialog::fitHeight()
{
    // Mismo calculo que Help: pulir todo antes de medir, o los labels se miden sin la fuente de
    // la hoja de estilo y el cartel queda corto.
    ensurePolished();
    for (QWidget *child : findChildren<QWidget *>()) {
        child->ensurePolished();
    }
    layout()->invalidate();
    layout()->activate();
    const int needed = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width())
                                                     : layout()->totalSizeHint().height();
    setMinimumHeight(needed);
    resize(width(), needed);
}

int ConfirmDialog::ask(QWidget *parent, const Spec &spec)
{
    // Puntero vigilado: si la ventana padre se cierra durante el exec, el cartel muere con ella.
    QPointer<ConfirmDialog> dialog = new ConfirmDialog(spec, parent);
    // El velo va sobre la ventana principal. Sobre otro cartel (Help, que ya trae el suyo) no:
    // taparia sus esquinas redondeadas con un rectangulo oscuro. Tampoco si la ventana ya tiene
    // uno: Help cerrado con reject() todavia no salio de su exec y su velo sigue puesto.
    QWidget *window = parent ? parent->window() : nullptr;
    bool alreadyDimmed = false;
    if (window) {
        for (Scrim *existing : window->findChildren<Scrim *>(Qt::FindDirectChildrenOnly)) {
            alreadyDimmed = alreadyDimmed || existing->isVisible();
        }
    }
    QPointer<Scrim> scrim;
    if (window && !qobject_cast<QDialog *>(window) && window->isVisible() && !alreadyDimmed) {
        scrim = new Scrim(window);
        scrim->show();
    }
    dialog->fitHeight();
    if (window && window->isVisible()) {
        const QPoint center = window->mapToGlobal(window->rect().center());
        dialog->move(center - QPoint(dialog->width() / 2, dialog->height() / 2));
    }
    dialog->exec();
    delete scrim.data();
    if (!dialog) {
        return spec.escapeIndex;
    }
    const int result = dialog->m_result;
    delete dialog.data();
    return result;
}

void ConfirmDialog::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Theme::color(Theme::kBorder), 1.0));
    painter.setBrush(Theme::color(Theme::kDialog));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}

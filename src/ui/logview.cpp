#include "videodownloader/logview.h"
#include "videodownloader/theme.h"
#include "videodownloader/uiwidgets.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QToolTip>
#include <QVBoxLayout>

namespace {

// Tope de lineas guardadas: al pasarlo se descartan las mas viejas de a bloques.
constexpr int MAX_ENTRIES = 5000;
constexpr int TRIM_CHUNK = 500;

// Geometria de fila del diseno: 12px de margen, columnas de 64 (hora) y 50 (nivel), 10 de separacion.
constexpr int PAD_X = 12;
constexpr int COL_TIME = 64;
constexpr int COL_LEVEL = 50;
constexpr int COL_GAP = 10;
constexpr int PAD_Y = 5;

const char *levelTag(LogLevel level)
{
    switch (level) {
    case LogLevel::Info: return "INFO";
    case LogLevel::Done: return "DONE";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Detail: return "";
    }
    return "";
}

} // namespace

// Boton de filtro: texto + contador en otro color, pintado a mano.
class FilterButton : public QPushButton
{
public:
    FilterButton(const QString &label, const QColor &countColor, QWidget *parent)
        : QPushButton(parent), m_label(label), m_countColor(countColor)
    {
        setCheckable(true);
        setAutoExclusive(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setFont(Theme::uiFont(11.5));
        setFixedHeight(22);
        // Sin estilo propio: la hoja global de QPushButton no debe tocar este boton.
        setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; padding: 0px; min-height: 22px; max-height: 22px; }"));
        updateWidth();
    }

    void setCount(int count)
    {
        if (count == m_count) {
            return;
        }
        m_count = count;
        updateWidth();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const bool on = isChecked();
        if (on || underMouse()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(on ? QColor(0x2a, 0x2a, 0x2a) : QColor(0x22, 0x22, 0x22));
            painter.drawRoundedRect(QRectF(rect()), 4, 4);
        }
        painter.setFont(font());
        const QFontMetrics fm(font());
        int x = 8;
        painter.setPen(on ? Theme::color(Theme::kTextStrong) : Theme::color(Theme::kTextCaption));
        painter.drawText(QRect(x, 0, fm.horizontalAdvance(m_label), height()), Qt::AlignLeft | Qt::AlignVCenter, m_label);
        x += fm.horizontalAdvance(m_label) + 5;
        painter.setPen(m_count > 0 ? m_countColor : Theme::color(Theme::kTextPlaceholder));
        const QString number = QString::number(m_count);
        painter.drawText(QRect(x, 0, fm.horizontalAdvance(number), height()), Qt::AlignLeft | Qt::AlignVCenter, number);
    }

private:
    void updateWidth()
    {
        const QFontMetrics fm(font());
        setFixedWidth(8 + fm.horizontalAdvance(m_label) + 5 + fm.horizontalAdvance(QString::number(m_count)) + 8);
    }

    QString m_label;
    QColor m_countColor;
    int m_count = 0;
};

// ------------------------------------------------------------------ LogCanvas

LogCanvas::LogCanvas(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setAutoFillBackground(false);
    viewport()->setMouseTracking(true);
    setFont(Theme::monoFont(12));
    verticalScrollBar()->setSingleStep(rowHeight());
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), QOverload<>::of(&QWidget::update));
}

int LogCanvas::rowHeight() const
{
    // line-height 1.25 del diseno + 3px arriba y abajo.
    return qRound(QFontInfo(font()).pixelSize() * 1.25) + 6;
}

void LogCanvas::append(const QString &text, LogLevel level, const QTime &time)
{
    const QScrollBar *bar = verticalScrollBar();
    // Si el usuario subio a leer algo, no se lo mueve; si estaba abajo, se sigue la salida.
    const bool followTail = bar->value() >= bar->maximum() - 2;

    m_entries.append({time, level, text});
    if (level == LogLevel::Warning) {
        ++m_warnings;
    } else if (level == LogLevel::Error) {
        ++m_errors;
    }

    if (m_entries.size() > MAX_ENTRIES + TRIM_CHUNK) {
        for (int i = 0; i < TRIM_CHUNK; ++i) {
            const LogLevel old = m_entries.at(i).level;
            if (old == LogLevel::Warning) {
                --m_warnings;
            } else if (old == LogLevel::Error) {
                --m_errors;
            }
        }
        m_entries.remove(0, TRIM_CHUNK);
        rebuildVisible();
    } else if (matches(m_entries.constLast())) {
        m_visible.append(m_entries.size() - 1);
    }

    updateScrollRange();
    if (followTail) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }
    viewport()->update();
    emit countsChanged();
}

void LogCanvas::clear()
{
    m_entries.clear();
    m_visible.clear();
    m_warnings = 0;
    m_errors = 0;
    updateScrollRange();
    viewport()->update();
    emit countsChanged();
}

void LogCanvas::setFilter(Filter filter)
{
    if (filter == m_filter) {
        return;
    }
    m_filter = filter;
    rebuildVisible();
    updateScrollRange();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    viewport()->update();
}

bool LogCanvas::matches(const Entry &entry) const
{
    switch (m_filter) {
    case Filter::All: return true;
    case Filter::Warnings: return entry.level == LogLevel::Warning;
    case Filter::Errors: return entry.level == LogLevel::Error;
    }
    return true;
}

void LogCanvas::rebuildVisible()
{
    m_visible.clear();
    m_visible.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        if (matches(m_entries.at(i))) {
            m_visible.append(i);
        }
    }
}

int LogCanvas::count(Filter filter) const
{
    switch (filter) {
    case Filter::All: return m_entries.size();
    case Filter::Warnings: return m_warnings;
    case Filter::Errors: return m_errors;
    }
    return 0;
}

QString LogCanvas::plainText() const
{
    QStringList lines;
    lines.reserve(m_visible.size());
    for (int index : m_visible) {
        const Entry &entry = m_entries.at(index);
        if (entry.level == LogLevel::Detail) {
            lines.append(QStringLiteral("                ") + entry.text);
        } else {
            lines.append(QStringLiteral("%1  %2  %3").arg(entry.time.toString(QStringLiteral("HH:mm:ss")),
                                                          QString::fromLatin1(levelTag(entry.level)).leftJustified(5),
                                                          entry.text));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void LogCanvas::updateScrollRange()
{
    const int content = m_visible.size() * rowHeight() + PAD_Y * 2;
    const int page = viewport()->height();
    verticalScrollBar()->setPageStep(page);
    verticalScrollBar()->setRange(0, qMax(0, content - page));
}

void LogCanvas::resizeEvent(QResizeEvent *event)
{
    const QScrollBar *bar = verticalScrollBar();
    const bool followTail = bar->value() >= bar->maximum() - 2;
    QAbstractScrollArea::resizeEvent(event);
    updateScrollRange();
    if (followTail) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }
}

bool LogCanvas::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        // Tooltip con la linea completa cuando el mensaje quedo recortado.
        const auto *help = static_cast<QHelpEvent *>(event);
        const int row = (help->pos().y() + verticalScrollBar()->value() - PAD_Y) / rowHeight();
        if (row >= 0 && row < m_visible.size()) {
            const Entry &entry = m_entries.at(m_visible.at(row));
            const int messageX = PAD_X + COL_TIME + COL_GAP + COL_LEVEL + COL_GAP;
            if (QFontMetrics(font()).horizontalAdvance(entry.text) > viewport()->width() - messageX - PAD_X) {
                QToolTip::showText(help->globalPos(), entry.text, viewport());
                return true;
            }
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void LogCanvas::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    const QRect area = viewport()->rect();
    // Fondo con las esquinas de abajo redondeadas como la tarjeta. Fuera del radio no se pinta
    // nada: se ve lo que hay detras (tarjeta y ventana), sin una cuna de otro gris.
    QPainterPath body;
    body.addRoundedRect(QRectF(area).adjusted(0, -8, 0, 0), 8, 8);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillPath(body, Theme::color(Theme::kLogBody));
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setClipPath(body);

    const int rowH = rowHeight();
    const int offset = verticalScrollBar()->value();
    int first = qMax(0, (offset - PAD_Y) / rowH);
    const int last = qMin<int>(m_visible.size() - 1, (offset + area.height() - PAD_Y) / rowH + 1);

    const QFont messageFont = font();
    QFont levelFont = font();
    levelFont.setPointSizeF(font().pointSizeF() * 10.5 / 12.0);
    levelFont.setWeight(QFont::Bold);
    levelFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
    const QFontMetrics fm(messageFont);

    const int timeX = PAD_X;
    const int levelX = timeX + COL_TIME + COL_GAP;
    const int messageX = levelX + COL_LEVEL + COL_GAP;
    const int messageWidth = qMax(0, area.width() - messageX - PAD_X);

    for (int row = first; row <= last; ++row) {
        const Entry &entry = m_entries.at(m_visible.at(row));
        const QRect rowRect(0, PAD_Y + row * rowH - offset, area.width(), rowH);

        QColor levelColor(0x6f, 0x6f, 0x6f);
        QColor messageColor(0x9a, 0x9a, 0x9a);
        switch (entry.level) {
        case LogLevel::Info: break;
        case LogLevel::Done: levelColor = QColor(0x8f, 0xbf, 0x5a); break;
        case LogLevel::Warning:
            painter.fillRect(rowRect, QColor(212, 164, 55, 15));
            levelColor = Theme::color(Theme::kWarn);
            messageColor = QColor(0xcd, 0xb5, 0x7a);
            break;
        case LogLevel::Error:
            painter.fillRect(rowRect, QColor(232, 131, 111, 20));
            levelColor = Theme::color(Theme::kError);
            messageColor = QColor(0xe3, 0xa5, 0x97);
            break;
        case LogLevel::Detail: messageColor = QColor(0x6f, 0x6f, 0x6f); break;
        }

        if (entry.level != LogLevel::Detail) {
            painter.setFont(messageFont);
            painter.setPen(Theme::color(Theme::kTextPlaceholder));
            painter.drawText(QRect(timeX, rowRect.y(), COL_TIME, rowH), Qt::AlignLeft | Qt::AlignVCenter,
                             entry.time.toString(QStringLiteral("HH:mm:ss")));
            painter.setFont(levelFont);
            painter.setPen(levelColor);
            painter.drawText(QRect(levelX, rowRect.y(), COL_LEVEL, rowH), Qt::AlignLeft | Qt::AlignVCenter,
                             QString::fromLatin1(levelTag(entry.level)));
        }
        painter.setFont(messageFont);
        painter.setPen(messageColor);
        painter.drawText(QRect(messageX, rowRect.y(), messageWidth, rowH), Qt::AlignLeft | Qt::AlignVCenter,
                         fm.elidedText(entry.text, Qt::ElideRight, messageWidth));
    }
}

// ------------------------------------------------------------------ LogView

LogView::LogView(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));
    setFixedHeight(196);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("logHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 9, 16, 7);
    headerLayout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("Log"), header);
    title->setObjectName(QStringLiteral("cardTitle"));
    headerLayout->addWidget(title);
    headerLayout->addSpacing(10);

    auto *filters = new QHBoxLayout();
    filters->setSpacing(4);
    m_all = new FilterButton(QStringLiteral("All"), Theme::color(Theme::kTextPlaceholder), header);
    m_warnings = new FilterButton(QStringLiteral("Warnings"), Theme::color(Theme::kWarn), header);
    m_errors = new FilterButton(QStringLiteral("Errors"), Theme::color(Theme::kError), header);
    m_all->setChecked(true);
    filters->addWidget(m_all);
    filters->addWidget(m_warnings);
    filters->addWidget(m_errors);
    headerLayout->addLayout(filters);
    headerLayout->addStretch(1);

    m_copy = Ui::button(QStringLiteral("Copy"), QStringLiteral("ghost"), QStringLiteral("sm"), header);
    Ui::setIcon(m_copy, Icon::Copy, Theme::color(Theme::kTextMuted));
    m_copy->setToolTip(QStringLiteral("Copy the lines shown to the clipboard"));
    m_clear = Ui::button(QStringLiteral("Clear"), QStringLiteral("ghost"), QStringLiteral("sm"), header);
    auto *actions = new QHBoxLayout();
    actions->setSpacing(4);
    actions->addWidget(m_copy);
    actions->addWidget(m_clear);
    headerLayout->addLayout(actions);

    m_canvas = new LogCanvas(this);
    layout->addWidget(header);
    layout->addWidget(m_canvas, 1);

    connect(m_all, &QPushButton::clicked, this, [this]() { m_canvas->setFilter(LogCanvas::Filter::All); });
    connect(m_warnings, &QPushButton::clicked, this, [this]() { m_canvas->setFilter(LogCanvas::Filter::Warnings); });
    connect(m_errors, &QPushButton::clicked, this, [this]() { m_canvas->setFilter(LogCanvas::Filter::Errors); });
    connect(m_copy, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_canvas->plainText());
    });
    connect(m_clear, &QPushButton::clicked, m_canvas, &LogCanvas::clear);
    connect(m_canvas, &LogCanvas::countsChanged, this, &LogView::refreshCounts);
}

void LogView::append(const QString &text, LogLevel level, const QTime &time)
{
    m_canvas->append(text, level, time);
}

void LogView::refreshCounts()
{
    m_all->setCount(m_canvas->count(LogCanvas::Filter::All));
    m_warnings->setCount(m_canvas->count(LogCanvas::Filter::Warnings));
    m_errors->setCount(m_canvas->count(LogCanvas::Filter::Errors));
}

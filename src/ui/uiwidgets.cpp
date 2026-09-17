#include "videodownloader/uiwidgets.h"
#include "videodownloader/theme.h"

#include <QHBoxLayout>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QtMath>

namespace {

// Arco de SVG (radio unico, sin rotacion) pasado a QPainterPath: centro desde los extremos.
void svgArc(QPainterPath &path, QPointF to, qreal r, bool largeArc, bool sweep)
{
    const QPointF from = path.currentPosition();
    const qreal dx = (from.x() - to.x()) / 2.0;
    const qreal dy = (from.y() - to.y()) / 2.0;
    const qreal d2 = dx * dx + dy * dy;
    if (d2 <= 0.0) {
        return;
    }
    if (d2 > r * r) {
        r = qSqrt(d2);
    }
    const qreal coef = qSqrt(qMax<qreal>(0.0, (r * r - d2) / d2)) * ((largeArc == sweep) ? -1.0 : 1.0);
    const QPointF center(coef * dy + (from.x() + to.x()) / 2.0, coef * -dx + (from.y() + to.y()) / 2.0);
    const qreal a1 = qAtan2(from.y() - center.y(), from.x() - center.x());
    const qreal a2 = qAtan2(to.y() - center.y(), to.x() - center.x());
    qreal delta = a2 - a1;
    if (sweep && delta < 0) {
        delta += 2 * M_PI;
    } else if (!sweep && delta > 0) {
        delta -= 2 * M_PI;
    }
    // SVG mide con y hacia abajo; Qt con angulos antihorarios: se invierten los signos.
    path.arcTo(QRectF(center.x() - r, center.y() - r, 2 * r, 2 * r), -qRadiansToDegrees(a1), -qRadiansToDegrees(delta));
}

struct IconSpec {
    qreal viewBox;
    qreal stroke;
    QPainterPath strokePath;
    QPainterPath fillPath;
};

IconSpec buildIcon(Icon icon)
{
    IconSpec s{16, 1.4, {}, {}};
    QPainterPath &p = s.strokePath;
    switch (icon) {
    case Icon::Help:
        s.viewBox = 20; s.stroke = 1.7;
        p.addEllipse(QPointF(10, 10), 8, 8);
        p.moveTo(7.6, 7.6);
        svgArc(p, QPointF(12.4, 8.5), 2.5, false, true);
        p.cubicTo(12.4, 10.2, 10, 10.6, 10, 11.9);
        s.fillPath.addEllipse(QPointF(10, 14.6), 1.25, 1.25);
        break;
    case Icon::Folder:
        s.viewBox = 16; s.stroke = 1.4;
        p.moveTo(1.8, 4.2);
        p.cubicTo(1.8, 3.6, 2.3, 3.1, 2.9, 3.1);
        p.lineTo(5.9, 3.1);
        p.lineTo(7.4, 4.7);
        p.lineTo(13.1, 4.7);
        p.cubicTo(13.7, 4.7, 14.2, 5.2, 14.2, 5.8);
        p.lineTo(14.2, 12.1);
        p.cubicTo(14.2, 12.7, 13.7, 13.2, 13.1, 13.2);
        p.lineTo(2.9, 13.2);
        p.cubicTo(2.3, 13.2, 1.8, 12.7, 1.8, 12.1);
        p.closeSubpath();
        break;
    case Icon::X:
        s.viewBox = 14; s.stroke = 1.5;
        p.moveTo(3.5, 3.5); p.lineTo(10.5, 10.5);
        p.moveTo(10.5, 3.5); p.lineTo(3.5, 10.5);
        break;
    case Icon::Retry:
        s.viewBox = 14; s.stroke = 1.5;
        p.moveTo(11.5, 7);
        svgArc(p, QPointF(10.2, 3.8), 4.5, true, true);
        p.moveTo(10.6, 1.6); p.lineTo(10.6, 4.2); p.lineTo(8, 4.2);
        break;
    case Icon::Check:
        s.viewBox = 14; s.stroke = 1.7;
        p.moveTo(3, 7.3); p.lineTo(5.6, 9.9); p.lineTo(11, 4.4);
        break;
    case Icon::Alert:
        s.viewBox = 14; s.stroke = 1.5;
        p.moveTo(7, 1.8); p.lineTo(12.6, 11.7); p.lineTo(1.4, 11.7); p.closeSubpath();
        p.moveTo(7, 5.6); p.lineTo(7, 8.4);
        s.fillPath.addEllipse(QPointF(7, 10.2), 1.05, 1.05);
        break;
    case Icon::Clock:
        s.viewBox = 14; s.stroke = 1.4;
        p.addEllipse(QPointF(7, 7), 5.3, 5.3);
        p.moveTo(7, 4.2); p.lineTo(7, 7); p.lineTo(8.9, 8.2);
        break;
    case Icon::Copy:
        s.viewBox = 14; s.stroke = 1.4;
        p.addRoundedRect(QRectF(4.5, 4.5, 7.5, 7.5), 1.3, 1.3);
        p.moveTo(9.5, 4.5); p.lineTo(9.5, 3.3);
        p.cubicTo(9.5, 2.6, 8.9, 2.0, 8.2, 2.0);
        p.lineTo(3.3, 2.0);
        p.cubicTo(2.6, 2.0, 2.0, 2.6, 2.0, 3.3);
        p.lineTo(2.0, 8.2);
        p.cubicTo(2.0, 8.9, 2.6, 9.5, 3.3, 9.5);
        p.lineTo(4.5, 9.5);
        break;
    case Icon::Cookie:
        s.viewBox = 16; s.stroke = 1.4;
        p.moveTo(8, 1.8);
        svgArc(p, QPointF(11.2, 5.0), 3, false, false);
        svgArc(p, QPointF(14.2, 8.0), 3, false, false);
        svgArc(p, QPointF(8, 1.8), 6.2, true, true);
        p.closeSubpath();
        s.fillPath.addEllipse(QPointF(5.6, 7), 0.5 + 0.7, 0.5 + 0.7);
        s.fillPath.addEllipse(QPointF(8.6, 10.4), 0.5 + 0.7, 0.5 + 0.7);
        s.fillPath.addEllipse(QPointF(5.4, 10.6), 0.5 + 0.7, 0.5 + 0.7);
        break;
    case Icon::Link:
        s.viewBox = 16; s.stroke = 1.4;
        p.moveTo(6.8, 9.2);
        svgArc(p, QPointF(10.8, 9.2), 2.8, false, false);
        p.lineTo(13, 7);
        svgArc(p, QPointF(9, 3), 2.8, false, false);
        p.lineTo(8.1, 3.9);
        p.moveTo(9.2, 6.8);
        svgArc(p, QPointF(5.2, 6.8), 2.8, false, false);
        p.lineTo(3, 9);
        svgArc(p, QPointF(7, 13), 2.8, false, false);
        p.lineTo(7.9, 12.1);
        break;
    case Icon::External:
        s.viewBox = 12; s.stroke = 1.3;
        p.moveTo(7, 2); p.lineTo(10, 2); p.lineTo(10, 5);
        p.moveTo(10, 2); p.lineTo(5.5, 6.5);
        p.moveTo(9, 7.5); p.lineTo(9, 10); p.lineTo(2, 10); p.lineTo(2, 3); p.lineTo(4.5, 3);
        break;
    case Icon::ArrowDown:
        s.viewBox = 14; s.stroke = 1.6;
        p.moveTo(7, 2.5); p.lineTo(7, 9.5);
        p.moveTo(3.8, 6.5); p.lineTo(7, 9.7); p.lineTo(10.2, 6.5);
        p.moveTo(3, 12); p.lineTo(11, 12);
        break;
    }
    return s;
}

class VectorIconEngine : public QIconEngine
{
public:
    VectorIconEngine(Icon icon, const QColor &color) : m_icon(icon), m_color(color) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        QColor c = m_color;
        if (mode == QIcon::Disabled) {
            c.setAlphaF(0.45);
        }
        Icons::paint(*painter, m_icon, rect, c);
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pm(size * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QIconEngine *clone() const override { return new VectorIconEngine(m_icon, m_color); }

private:
    Icon m_icon;
    QColor m_color;
};

} // namespace

namespace Icons {

void paint(QPainter &painter, Icon icon, const QRectF &rect, const QColor &color)
{
    const IconSpec spec = buildIcon(icon);
    const qreal scale = qMin(rect.width(), rect.height()) / spec.viewBox;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.center().x() - spec.viewBox * scale / 2.0, rect.center().y() - spec.viewBox * scale / 2.0);
    painter.scale(scale, scale);
    QPen pen(color, spec.stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(spec.strokePath);
    if (!spec.fillPath.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(spec.fillPath);
    }
    painter.restore();
}

QIcon icon(Icon icon, const QColor &color)
{
    return QIcon(new VectorIconEngine(icon, color));
}

} // namespace Icons

// ------------------------------------------------------------------ IconWidget

IconWidget::IconWidget(Icon icon, const QColor &color, int size, QWidget *parent)
    : QWidget(parent), m_icon(icon), m_color(color)
{
    setFixedSize(size, size);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void IconWidget::setIcon(Icon icon, const QColor &color)
{
    if (icon == m_icon && color == m_color) {
        return;
    }
    m_icon = icon;
    m_color = color;
    update();
}

void IconWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    Icons::paint(painter, m_icon, rect(), m_color);
}

// ------------------------------------------------------------------ ElidedLabel

ElidedLabel::ElidedLabel(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

void ElidedLabel::setText(const QString &text)
{
    if (text == m_text) {
        return;
    }
    m_text = text;
    setToolTip(text);
    updateGeometry();
    update();
}

void ElidedLabel::setElideMode(Qt::TextElideMode mode)
{
    m_mode = mode;
    update();
}

QSize ElidedLabel::sizeHint() const
{
    const QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(m_text), fm.height());
}

QSize ElidedLabel::minimumSizeHint() const
{
    return QSize(0, QFontMetrics(font()).height());
}

void ElidedLabel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setFont(font());
    painter.setPen(palette().color(QPalette::WindowText));
    const QString shown = fontMetrics().elidedText(m_text, m_mode, width());
    painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, shown);
}

// ------------------------------------------------------------------ Chip

Chip::Chip(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("chip"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 7, 0);
    layout->setSpacing(5);
    m_icon = new IconWidget(Icon::Check, Theme::color(Theme::kOk), 14, this);
    m_label = new QLabel(this);
    layout->addWidget(m_icon);
    layout->addWidget(m_label);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void Chip::set(const QString &tone, const QString &text, bool hasIcon, Icon icon)
{
    if (tone != m_tone) {
        m_tone = tone;
        setProperty("tone", tone);
        Ui::repolish(this);
        Ui::repolish(m_label);
    }
    m_label->setText(text);
    QColor iconColor(QStringLiteral("#c5c8c7"));
    if (tone == QLatin1String("ok")) {
        iconColor = Theme::color(Theme::kOk);
    } else if (tone == QLatin1String("err")) {
        iconColor = Theme::color(Theme::kError);
    } else if (tone == QLatin1String("warn")) {
        iconColor = Theme::color(Theme::kWarn);
    } else if (tone == QLatin1String("run")) {
        iconColor = Theme::color(Theme::kRun);
    }
    m_icon->setIcon(icon, iconColor);
    m_icon->setVisible(hasIcon);
    layout()->setContentsMargins(hasIcon ? 6 : 7, 0, 7, 0);
}

// ------------------------------------------------------------------ ProgressLine

ProgressLine::ProgressLine(int height, QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(height);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ProgressLine::setValue(double fraction, Tone tone)
{
    fraction = qBound(0.0, fraction, 1.0);
    if (qFuzzyCompare(fraction + 1.0, m_fraction + 1.0) && tone == m_tone) {
        return;
    }
    m_fraction = fraction;
    m_tone = tone;
    update();
}

void ProgressLine::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = height() / 2.0;
    const bool idle = m_tone == Tone::Idle;
    painter.setPen(QPen(idle ? QColor(0x30, 0x30, 0x30) : Theme::color(Theme::kProgressBorder), 1.0));
    painter.setBrush(idle ? Theme::color(Theme::kTile) : Theme::color(Theme::kProgressTrack));
    painter.drawRoundedRect(outer, radius, radius);
    if (m_fraction <= 0.0) {
        return;
    }
    const QRectF inner = QRectF(rect()).adjusted(1, 1, -1, -1);
    QRectF fill = inner;
    fill.setWidth(inner.width() * m_fraction);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_tone == Tone::Done ? Theme::color(Theme::kProgressDone) : Theme::color(Theme::kProgressFill));
    const qreal innerRadius = qMax<qreal>(0.0, radius - 1.0);
    painter.drawRoundedRect(fill, innerRadius, innerRadius);
}

// ------------------------------------------------------------------ StatusBadge

StatusBadge::StatusBadge(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(28, 28);
}

void StatusBadge::setKind(Kind kind)
{
    if (kind == m_kind) {
        return;
    }
    m_kind = kind;
    update();
}

void StatusBadge::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor bg(0x2e, 0x2e, 0x2e);
    QColor fg(0x7b, 0x7b, 0x7b);
    Icon icon = Icon::Clock;
    switch (m_kind) {
    case Kind::Done: bg = QColor(0x1f, 0x2a, 0x17); fg = Theme::color(Theme::kOk); icon = Icon::Check; break;
    case Kind::Running: bg = QColor(0x2c, 0x27, 0x50); fg = Theme::color(Theme::kRun); icon = Icon::ArrowDown; break;
    case Kind::Error: bg = QColor(0x35, 0x21, 0x1f); fg = Theme::color(Theme::kError); icon = Icon::Alert; break;
    case Kind::Waiting: break;
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawEllipse(QRectF(rect()));
    Icons::paint(painter, icon, QRectF(7, 7, 14, 14), fg);
}

// ------------------------------------------------------------------ ArrowComboBox

ArrowComboBox::ArrowComboBox(QWidget *parent)
    : QComboBox(parent)
{
}

void ArrowComboBox::paintEvent(QPaintEvent *event)
{
    QComboBox::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x7b, 0x7b, 0x7b));
    // Triangulo de 8x5 centrado en la zona de 22px de la derecha.
    const qreal cx = width() - 11.0;
    const qreal cy = height() / 2.0;
    QPainterPath caret;
    caret.moveTo(cx - 4.0, cy - 2.5);
    caret.lineTo(cx + 4.0, cy - 2.5);
    caret.lineTo(cx, cy + 2.5);
    caret.closeSubpath();
    painter.drawPath(caret);
}

// ------------------------------------------------------------------ Ui

namespace Ui {

QPushButton *button(const QString &text, const QString &variant, const QString &size, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    if (!variant.isEmpty()) {
        b->setProperty("variant", variant);
    }
    if (!size.isEmpty()) {
        b->setProperty("btnSize", size);
    }
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::TabFocus);
    return b;
}

void setIcon(QPushButton *button, Icon icon, const QColor &color, int size)
{
    button->setIcon(Icons::icon(icon, color));
    button->setIconSize(QSize(size, size));
}

void repolish(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace Ui

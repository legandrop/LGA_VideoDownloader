#include "videodownloader/tabheader.h"
#include "videodownloader/theme.h"
#include "videodownloader/uiwidgets.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QtMath>

namespace {

constexpr int BAR_HEIGHT = 50;
constexpr int TAB_PADDING_X = 22;
const char *const TAB_TEXT = "DOWNLOADS";

QFont tabFont()
{
    QFont font = Theme::uiFont(15, QFont::Medium);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.2);
    return font;
}

// Boton de esquina (37x50) con el icono de ayuda; se aclara al pasar el mouse.
class CornerButton : public QAbstractButton
{
public:
    explicit CornerButton(QWidget *parent) : QAbstractButton(parent)
    {
        setFixedSize(37, BAR_HEIGHT);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setAttribute(Qt::WA_Hover);
        setToolTip(QStringLiteral("Help, updates and credits"));
        setAccessibleName(QStringLiteral("Help"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QColor color = underMouse() || hasFocus() ? Theme::color(Theme::kText) : Theme::color(Theme::kIcon);
        Icons::paint(painter, Icon::Help, QRectF((width() - 20) / 2.0, (height() - 20) / 2.0, 20, 20), color);
    }
};

QPushButton *noticeButton(QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setObjectName(QStringLiteral("noticeChip"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::TabFocus);
    button->hide();
    return button;
}

} // namespace

TabHeader::TabHeader(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(BAR_HEIGHT);
    auto *layout = new QHBoxLayout(this);
    const int tabWidth = qFloor(QFontMetricsF(tabFont()).horizontalAdvance(QLatin1String(TAB_TEXT))) + TAB_PADDING_X * 2 + 2;
    layout->setContentsMargins(tabWidth, 0, 8, 0);
    layout->setSpacing(8);
    layout->addStretch(1);

    m_toolsNotice = noticeButton(this);
    m_updateNotice = noticeButton(this);
    m_updateNotice->setToolTip(QStringLiteral("A new version is ready. Open Help to update."));
    layout->addWidget(m_toolsNotice, 0, Qt::AlignVCenter);
    layout->addWidget(m_updateNotice, 0, Qt::AlignVCenter);

    auto *help = new CornerButton(this);
    layout->addWidget(help);

    connect(help, &QAbstractButton::clicked, this, &TabHeader::helpClicked);
    connect(m_toolsNotice, &QPushButton::clicked, this, &TabHeader::toolsNoticeClicked);
    connect(m_updateNotice, &QPushButton::clicked, this, &TabHeader::updateNoticeClicked);
}

void TabHeader::setToolsNotice(const QString &text, const QString &tone, const QString &tooltip)
{
    m_toolsNotice->setText(text);
    m_toolsNotice->setToolTip(tooltip);
    if (m_toolsNotice->property("tone").toString() != tone) {
        m_toolsNotice->setProperty("tone", tone);
        Ui::repolish(m_toolsNotice);
    }
    m_toolsNotice->setVisible(!text.isEmpty());
}

void TabHeader::setUpdateNotice(const QString &text)
{
    m_updateNotice->setText(text);
    m_updateNotice->setVisible(!text.isEmpty());
}

void TabHeader::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Theme::color(Theme::kTabBar));
    // Linea inferior de la barra; la pestana activa la tapa.
    painter.fillRect(QRect(0, BAR_HEIGHT - 1, width(), 1), Theme::color(Theme::kBorder));

    const QFont font = tabFont();
    // Ancho fraccionario redondeado hacia abajo, como lo resuelve el navegador del diseno.
    const int textWidth = qFloor(QFontMetricsF(font).horizontalAdvance(QLatin1String(TAB_TEXT)));
    const QRectF tab(0.5, 0.5, textWidth + TAB_PADDING_X * 2 + 1, BAR_HEIGHT + 4);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Theme::color(Theme::kBorder), 1.0));
    painter.setBrush(Theme::color(Theme::kWindow));
    painter.setClipRect(QRect(0, 0, width(), BAR_HEIGHT));
    painter.drawRoundedRect(tab, 4, 4);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Theme::color(Theme::kAccent));
    painter.setFont(font);
    painter.drawText(QRectF(1, 0, textWidth + TAB_PADDING_X * 2, BAR_HEIGHT), Qt::AlignCenter, QLatin1String(TAB_TEXT));
}

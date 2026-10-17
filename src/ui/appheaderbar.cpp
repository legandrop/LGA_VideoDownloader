#include "videodownloader/appheaderbar.h"
#include "videodownloader/theme.h"
#include "videodownloader/titlebarbuttons.h"
#include "videodownloader/uiwidgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QPushButton>

namespace {

// Medidas de la barra de FaceLock y RotoWorker (AppHeaderBar.cpp de RotoWorker), sin re-derivar.
constexpr int kLineHeight = 1;
constexpr int kHeaderGap = 14;
constexpr int kLeftMargin = 14;
constexpr int kRightMarginNoButtons = 12; // sin botones de ventana (fuera de Windows)
constexpr int kLogoSize = 22;
constexpr int kBrandGap = 8;              // logo -> nombre
constexpr int kNoticeGap = 8;             // entre los dos avisos
constexpr int kRightIconGap = 18;
constexpr int kSeparatorHeight = 18;
constexpr int kWindowButtonWidth = 40;
constexpr int kContentYOffset = 1;        // logo y nombre, 1 px bajo el centro de la fila

// Icono real de la app (el PNG de 256 del mismo .ico del .exe y de la barra de tareas) al tamano
// fisico pedido, sin tinte. Igual que renderLogoPixmap() de RotoWorker y FaceLock.
QPixmap renderLogoPixmap(int logicalSize, qreal dpr)
{
    const QString cacheKey = QStringLiteral("vd_appbar_logo:%1:%2").arg(logicalSize).arg(dpr);
    QPixmap cached;
    if (QPixmapCache::find(cacheKey, &cached)) {
        return cached;
    }
    const QPixmap source(QStringLiteral(":/icons/app_256.png"));
    const QSize physicalSize(qRound(logicalSize * dpr), qRound(logicalSize * dpr));
    QPixmap pixmap = source.scaled(physicalSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(dpr);
    QPixmapCache::insert(cacheKey, pixmap);
    return pixmap;
}

// Logo de la barra: se pinta en cada paintEvent con el DPR del monitor donde esta la ventana.
class AppLogo : public QWidget
{
public:
    explicit AppLogo(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(kLogoSize, kLogoSize);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawPixmap(rect(), renderLogoPixmap(kLogoSize, devicePixelRatioF()));
    }
};

// Separador vertical de 1 px en el color de la linea de la barra.
class BarSeparator : public QWidget
{
public:
    BarSeparator(QWidget *parent, int height)
        : QWidget(parent)
    {
        setFixedSize(1, height);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Theme::color(Theme::kBorder));
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

AppHeaderBar::AppHeaderBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("appHeaderBar"));
    setFixedHeight(kBarHeight + kLineHeight);

    auto *row = new QHBoxLayout(this);
    m_rowLayout = row;
    row->setContentsMargins(kLeftMargin, 0, 0, kLineHeight);
    row->setSpacing(kHeaderGap);

    // Zona izquierda y central (logo, nombre, avisos): su contenido va 1 px mas abajo que el centro de
    // la fila, como en FaceLock y RotoWorker. Los iconos y los botones de la derecha van centrados.
    auto *content = new QWidget(this);
    content->setObjectName(QStringLiteral("appHeaderContent"));
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, kContentYOffset, 0, 0);
    contentLayout->setSpacing(kBrandGap);
    row->addWidget(content, 1);

    auto *logo = new AppLogo(content);
    logo->setObjectName(QStringLiteral("appLogo"));
    contentLayout->addWidget(logo, 0, Qt::AlignVCenter);
    auto *name = new QLabel(QStringLiteral("Video Downloader"), content);
    name->setObjectName(QStringLiteral("appName"));
    contentLayout->addWidget(name, 0, Qt::AlignVCenter);
    contentLayout->addStretch(1);

    // Avisos discretos (tools, update): los mismos chips que tenia la barra de pestanas.
    m_toolsNotice = noticeButton(content);
    m_updateNotice = noticeButton(content);
    m_updateNotice->setToolTip(QStringLiteral("See what's new and install it"));
    contentLayout->addWidget(m_toolsNotice, 0, Qt::AlignVCenter);
    contentLayout->addSpacing(kNoticeGap - kBrandGap);
    contentLayout->addWidget(m_updateNotice, 0, Qt::AlignVCenter);

    // Settings y Help, iguales a los de FaceLock y RotoWorker: los mismos dibujos (resources/icons/
    // settings.svg y help.svg, byte a byte), 20 px, sin fondo, 18 px entre los dos y 18 hasta el separador.
    m_settingsButton = new VdBarIconButton(this, QStringLiteral(":/icons/settings.svg"), kBarHeight,
                                           [this]() { emit settingsClicked(); });
    m_settingsButton->setObjectName(QStringLiteral("appSettingsButton"));
    m_settingsButton->setCursor(Qt::PointingHandCursor);
    m_settingsButton->setToolTip(QStringLiteral("Settings: updates, browser extension and download tools"));
    m_settingsButton->setAccessibleName(QStringLiteral("Settings"));
    m_helpButton = new VdBarIconButton(this, QStringLiteral(":/icons/help.svg"), kBarHeight,
                                       [this]() { emit helpClicked(); });
    m_helpButton->setObjectName(QStringLiteral("appHelpButton"));
    m_helpButton->setCursor(Qt::PointingHandCursor);
    m_helpButton->setToolTip(QStringLiteral("Help: what's new, about the app and credits"));
    m_helpButton->setAccessibleName(QStringLiteral("Help"));
    row->addWidget(m_settingsButton);
    row->addSpacing(kRightIconGap - kHeaderGap);
    row->addWidget(m_helpButton);

    // Botones de ventana: separador + minimizar, maximizar/restaurar y cerrar, pegados al borde
    // derecho. Los 18 px hasta Help se completan con el gap de la fila.
    m_windowControls = new QWidget(this);
    m_windowControls->setObjectName(QStringLiteral("appWindowControls"));
    auto *controls = new QHBoxLayout(m_windowControls);
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(0);
    controls->addSpacing(kRightIconGap - kHeaderGap);
    controls->addWidget(new BarSeparator(m_windowControls, kSeparatorHeight), 0, Qt::AlignVCenter);
    controls->addSpacing(kRightIconGap);
    const QSize buttonSize(kWindowButtonWidth, kBarHeight);
    m_minimizeButton = new VdWindowButton(m_windowControls, VdWindowButton::Kind::Minimize, buttonSize,
                                          [this]() { emit minimizeRequested(); });
    m_minimizeButton->setObjectName(QStringLiteral("appWindowMinimize"));
    m_maximizeButton = new VdWindowButton(m_windowControls, VdWindowButton::Kind::MaximizeRestore, buttonSize,
                                          [this]() { emit maximizeRestoreRequested(); });
    m_maximizeButton->setObjectName(QStringLiteral("appWindowMaximize"));
    m_closeButton = new VdWindowButton(m_windowControls, VdWindowButton::Kind::Close, buttonSize,
                                       [this]() { emit closeRequested(); });
    m_closeButton->setObjectName(QStringLiteral("appWindowClose"));
    controls->addWidget(m_minimizeButton);
    controls->addWidget(m_maximizeButton);
    controls->addWidget(m_closeButton);
    row->addWidget(m_windowControls);

    connect(m_toolsNotice, &QPushButton::clicked, this, &AppHeaderBar::toolsNoticeClicked);
    connect(m_updateNotice, &QPushButton::clicked, this, &AppHeaderBar::updateNoticeClicked);
}

void AppHeaderBar::setToolsNotice(const QString &text, const QString &tone, const QString &tooltip)
{
    m_toolsNotice->setText(text);
    m_toolsNotice->setToolTip(tooltip);
    if (m_toolsNotice->property("tone").toString() != tone) {
        m_toolsNotice->setProperty("tone", tone);
        Ui::repolish(m_toolsNotice);
    }
    m_toolsNotice->setVisible(!text.isEmpty());
}

void AppHeaderBar::setUpdateNotice(const QString &text)
{
    m_updateNotice->setText(text);
    m_updateNotice->setVisible(!text.isEmpty());
}

void AppHeaderBar::setSettingsActive(bool active)
{
    m_settingsButton->setActive(active);
}

void AppHeaderBar::setHelpActive(bool active)
{
    m_helpButton->setActive(active);
}

void AppHeaderBar::setWindowButtonsVisible(bool visible)
{
    m_windowControls->setVisible(visible);
    m_rowLayout->setContentsMargins(kLeftMargin, 0, visible ? 0 : kRightMarginNoButtons, kLineHeight);
}

void AppHeaderBar::setWindowMaximized(bool maximized)
{
    m_maximizeButton->setMaximized(maximized);
}

bool AppHeaderBar::isDragAreaAt(const QPoint &posInBar) const
{
    if (!rect().contains(posInBar)) {
        return false;
    }
    // Como en FaceLock y RotoWorker: el grupo de la derecha entero, con sus huecos y su separador, NO
    // arrastra; empieza en el primer control visible de ese grupo.
    int clusterLeft = width();
    const QWidget *const rightCluster[] = {
        m_toolsNotice, m_updateNotice, m_settingsButton, m_helpButton, m_windowControls,
    };
    for (const QWidget *control : rightCluster) {
        if (control && control->isVisibleTo(this)) {
            clusterLeft = qMin(clusterLeft, control->mapTo(this, QPoint(0, 0)).x());
        }
    }
    return posInBar.x() < clusterLeft;
}

void AppHeaderBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Theme::color(Theme::kTitleBar));
    // Linea inferior de 1 px, la de FaceLock y RotoWorker.
    painter.fillRect(QRect(0, height() - kLineHeight, width(), kLineHeight), Theme::color(Theme::kBorder));
}

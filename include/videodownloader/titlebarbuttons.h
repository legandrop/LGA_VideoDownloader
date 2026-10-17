#ifndef TITLEBARBUTTONS_H
#define TITLEBARBUTTONS_H

// Botones de la barra de titulo propia, copiados de LGA_RotoWorker (include/rotoworker/ui/shell/WindowButton.h
// y BarIconButton.h), que los trae de LGA_FaceLock (FaceLockWindowButton y FaceLockAppBarIconButton). Sin
// cambios de dibujo ni de comportamiento: las clases pasan a VdWindowButton y VdBarIconButton, y los colores
// salen de Theme con los mismos valores. Header-only, como los originales.

#include "videodownloader/theme.h"

#include <QColor>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPixmapCache>
#include <QSize>
#include <QSvgRenderer>
#include <QWidget>

#include <functional>
#include <utility>

/**
 * Boton de ventana (minimizar / maximizar-restaurar / cerrar) de la barra de titulo propia.
 *
 * Sin fondo ni borde propios: el hover pinta el rect COMPLETO, que es lo que hace que el
 * boton se lea como parte de la barra y no como una cajita apoyada encima.
 */
class VdWindowButton : public QWidget
{
public:
    enum class Kind
    {
        Minimize,
        MaximizeRestore,
        Close,
    };

    VdWindowButton(QWidget *parent, Kind kind, QSize size, std::function<void()> onClick)
        : QWidget(parent)
        , m_kind(kind)
        , m_onClick(std::move(onClick))
    {
        setAttribute(Qt::WA_Hover, true);
        setFixedSize(size);
    }

    void setMaximized(bool maximized)
    {
        if (m_maximized == maximized) {
            return;
        }
        m_maximized = maximized;
        update();
    }

    bool isMaximizedGlyph() const { return m_maximized; }

protected:
    // El hover se lee de HoverEnter/HoverLeave (WA_Hover), igual que en el original. HoverMove
    // ademas repone el estado si el HoverEnter se perdio.
    bool event(QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::HoverEnter:
        case QEvent::HoverMove:
            if (!m_hovered) {
                m_hovered = true;
                update();
            }
            break;
        case QEvent::HoverLeave:
            if (m_hovered) {
                m_hovered = false;
                update();
            }
            break;
        default:
            break;
        }
        return QWidget::event(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_onClick) {
            m_onClick();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QColor glyphColor = Theme::color(Theme::kTitleIcon);
        if (m_hovered) {
            const bool isClose = (m_kind == Kind::Close);
            painter.fillRect(rect(), Theme::color(isClose ? Theme::kCloseButtonHover : Theme::kWindowButtonHover));
            glyphColor = Theme::color(isClose ? Theme::kCloseButtonHoverIcon : Theme::kTitleIconHover);
        }

        QPen pen(glyphColor);
        pen.setWidthF(1.0);
        painter.setPen(pen);

        const qreal cx = width() / 2.0;
        const qreal cy = height() / 2.0;

        // Tamanos de caja medidos por pixel en el original: minimizar 9 px de ancho,
        // maximizar/cerrar 10x10.
        switch (m_kind) {
        case Kind::Minimize: {
            painter.setRenderHint(QPainter::Antialiasing, false);
            const int y = qRound(cy);
            painter.drawLine(QPointF(cx - 4.0, y + 0.5), QPointF(cx + 4.0, y + 0.5));
            break;
        }
        case Kind::MaximizeRestore: {
            painter.setRenderHint(QPainter::Antialiasing, false);
            if (m_maximized) {
                // Restaurar: cuadrado de adelante + esquina del de atras.
                painter.drawRect(QRectF(cx - 4.5 + 0.5, cy - 2.5 + 0.5, 7.0, 7.0));
                painter.drawLine(QPointF(cx - 2.0, cy - 4.0), QPointF(cx + 5.0, cy - 4.0));
                painter.drawLine(QPointF(cx + 5.0, cy - 4.0), QPointF(cx + 5.0, cy + 3.0));
            } else {
                painter.drawRect(QRectF(cx - 4.5 + 0.5, cy - 4.5 + 0.5, 9.0, 9.0));
            }
            break;
        }
        case Kind::Close: {
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.drawLine(QPointF(cx - 4.0, cy - 4.0), QPointF(cx + 4.0, cy + 4.0));
            painter.drawLine(QPointF(cx + 4.0, cy - 4.0), QPointF(cx - 4.0, cy + 4.0));
            break;
        }
        }
    }

private:
    Kind m_kind;
    std::function<void()> m_onClick;
    bool m_hovered = false;
    bool m_maximized = false;
};

/**
 * Boton de icono de la derecha de la barra de titulo propia (Settings / Help): el icono de 20 px
 * tintado, sin fondo propio, en una caja de 20 px de ancho y el alto de la barra. Se aclara con el
 * cursor encima y mientras su dialogo esta abierto (setActive), y dispara al apretar.
 */
class VdBarIconButton : public QWidget
{
public:
    static constexpr int kIconSize = 20;

    VdBarIconButton(QWidget *parent, const QString &resourcePath, int barContentHeight, std::function<void()> onClick)
        : QWidget(parent)
        , m_resourcePath(resourcePath)
        , m_onClick(std::move(onClick))
    {
        setAttribute(Qt::WA_Hover, true);
        setFixedSize(kIconSize, barContentHeight);
    }

    // El dialogo que abre este boton esta abierto: el icono queda en el gris de «cursor encima».
    void setActive(bool active)
    {
        if (m_active == active) {
            return;
        }
        m_active = active;
        update();
    }

    bool isActive() const { return m_active; }

    static QPixmap renderTintedSvg(const QString &resourcePath, int logicalSize, const QColor &color, qreal dpr)
    {
        const QString cacheKey = QStringLiteral("vd_appbar:%1:%2:%3:%4")
                                     .arg(resourcePath)
                                     .arg(logicalSize)
                                     .arg(color.name(QColor::HexArgb))
                                     .arg(dpr);
        QPixmap cached;
        if (QPixmapCache::find(cacheKey, &cached)) {
            return cached;
        }

        QSvgRenderer renderer(resourcePath);
        QPixmap pixmap(QSize(qRound(logicalSize * dpr), qRound(logicalSize * dpr)));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);
        {
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            const QRectF target(0.0, 0.0, logicalSize, logicalSize);
            if (renderer.isValid()) {
                renderer.render(&painter, target);
            }
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(target, color);
        }
        QPixmapCache::insert(cacheKey, pixmap);
        return pixmap;
    }

protected:
    bool event(QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::HoverEnter:
        case QEvent::HoverMove:
            if (!m_hovered) {
                m_hovered = true;
                update();
            }
            break;
        case QEvent::HoverLeave:
            if (m_hovered) {
                m_hovered = false;
                update();
            }
            break;
        default:
            break;
        }
        return QWidget::event(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_onClick) {
            m_onClick();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QColor color = Theme::color((m_hovered || m_active) ? Theme::kTitleIconHover : Theme::kTitleIcon);
        const QPixmap icon = renderTintedSvg(m_resourcePath, kIconSize, color, devicePixelRatioF());
        painter.drawPixmap(0, (height() - kIconSize) / 2, icon);
    }

private:
    QString m_resourcePath;
    std::function<void()> m_onClick;
    bool m_hovered = false;
    bool m_active = false;
};

#endif // TITLEBARBUTTONS_H

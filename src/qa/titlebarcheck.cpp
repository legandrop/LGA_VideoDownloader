// `--qa-titlebar`: prueba del marco propio de la ventana con un HWND real (Windows). Arma la ventana
// en modo captura (sin tools, cola, red ni settings del usuario), la muestra y le pregunta a Windows
// que hay bajo cada punto (WM_NCHITTEST), y aprieta los botones de ventana de la barra. Se corre
// SIEMPRE por el runner sin escritorio, con `-Platform windows`. No prueba lo que hace Windows con
// entrada real (arrastre, resize con el mouse, Snap, Win+flechas, sombra): eso es a mano.

#include "videodownloader/uishot.h"
#include "videodownloader/appheaderbar.h"
#include "videodownloader/mainwindow.h"
#include "videodownloader/titlebarbuttons.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QScreen>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
void settle(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

// La accion de los botones de la barra sale al apretar el boton del mouse, como en FaceLock.
void press(QWidget *button)
{
    const QPointF local(button->width() / 2.0, button->height() / 2.0);
    QMouseEvent event(QEvent::MouseButtonPress, local, button->mapToGlobal(local), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(button, &event);
}

struct Checker {
    int passed = 0;
    int total = 0;
    void check(bool ok, const char *what, const QString &detail = QString())
    {
        ++total;
        passed += ok ? 1 : 0;
        fprintf(stdout, "%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.isEmpty() ? "" : " · ",
                qPrintable(detail));
    }
};

// Respuesta de WM_NCHITTEST en un punto LOGICO de la ventana.
LRESULT hitAt(HWND hwnd, const QWidget *window, QPoint logical)
{
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    const qreal dpr = window->devicePixelRatioF() > 0.0 ? window->devicePixelRatioF() : 1.0;
    const int x = rect.left + qRound(logical.x() * dpr);
    const int y = rect.top + qRound(logical.y() * dpr);
    return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(static_cast<WORD>(static_cast<short>(x)),
                                                          static_cast<WORD>(static_cast<short>(y))));
}

QPoint centerIn(const QWidget *widget, const QWidget *window)
{
    return widget->mapTo(window, QPoint(widget->width() / 2, widget->height() / 2));
}
#endif

} // namespace

int runTitleBarCheck(const QStringList &)
{
#ifndef Q_OS_WIN
    fprintf(stdout, "titlebar-qa result=skip (only Windows has the custom frame)\n");
    return 0;
#else
    if (QGuiApplication::platformName() != QLatin1String("windows")) {
        fprintf(stderr, "titlebar-qa: needs a real window (-Platform windows); platform=%s\n",
                qPrintable(QGuiApplication::platformName()));
        return 2;
    }

    MainWindow window(MainWindow::Mode::Capture);
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    window.resize(qMin(1100, available.width() - 80), qMin(760, available.height() - 80));
    window.move(available.topLeft() + QPoint(40, 40));
    window.show();
    settle(400);

    AppHeaderBar *bar = window.headerBar();
    const HWND hwnd = reinterpret_cast<HWND>(window.winId());
    Checker c;

    // Estilo nativo repuesto (Snap, Win+flechas, animacion de minimizado) y sin marco dibujado.
    const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    c.check((style & WS_THICKFRAME) && (style & WS_CAPTION) && !(style & WS_POPUP), "estilo WS_OVERLAPPEDWINDOW",
            QStringLiteral("style=0x%1").arg(quint64(style), 0, 16));
    RECT windowRect{};
    RECT clientRect{};
    GetWindowRect(hwnd, &windowRect);
    GetClientRect(hwnd, &clientRect);
    c.check(clientRect.right == windowRect.right - windowRect.left && clientRect.bottom == windowRect.bottom - windowRect.top,
            "cliente igual a la ventana",
            QStringLiteral("window=%1x%2 client=%3x%4")
                .arg(windowRect.right - windowRect.left).arg(windowRect.bottom - windowRect.top)
                .arg(clientRect.right).arg(clientRect.bottom));
    c.check(bar->height() == AppHeaderBar::kBarHeight + 1, "barra de 43 + 1 px", QString::number(bar->height()));
    c.check(bar->closeButton()->isVisible(), "botones de ventana visibles");

    // Que hay bajo cada punto.
    const int w = window.width();
    const int h = window.height();
    const QPoint barMiddle(w / 2, AppHeaderBar::kBarHeight / 2);
    const QPoint settingsCenter = centerIn(bar->settingsButton(), &window);
    const QPoint helpCenter = centerIn(bar->helpButton(), &window);
    const struct { const char *what; QPoint point; LRESULT expected; } hits[] = {
        {"hueco del medio de la barra arrastra", barMiddle, HTCAPTION},
        {"logo y nombre arrastran", QPoint(60, AppHeaderBar::kBarHeight / 2), HTCAPTION},
        {"Settings es cliente", settingsCenter, HTCLIENT},
        {"Help es cliente", helpCenter, HTCLIENT},
        {"hueco entre Settings y Help no arrastra", QPoint((settingsCenter.x() + helpCenter.x()) / 2, settingsCenter.y()), HTCLIENT},
        {"cerrar es cliente", centerIn(bar->closeButton(), &window), HTCLIENT},
        {"cuerpo es cliente", QPoint(w / 2, h / 2), HTCLIENT},
        {"borde izquierdo", QPoint(2, h / 2), HTLEFT},
        {"borde derecho", QPoint(w - 3, h / 2), HTRIGHT},
        {"borde de arriba", QPoint(w / 2, 2), HTTOP},
        {"borde de abajo", QPoint(w / 2, h - 3), HTBOTTOM},
        {"esquina de arriba a la izquierda", QPoint(2, 2), HTTOPLEFT},
        {"esquina de abajo a la derecha", QPoint(w - 3, h - 3), HTBOTTOMRIGHT},
    };
    for (const auto &hit : hits) {
        const LRESULT got = hitAt(hwnd, &window, hit.point);
        c.check(got == hit.expected, hit.what, QStringLiteral("hit=%1 esperado=%2").arg(got).arg(hit.expected));
    }

    // Maximizar con el boton de la barra: el contenido ocupa el area de trabajo del monitor.
    const QRect normalBefore = window.geometry();
    press(bar->maximizeButton());
    settle(600);
    {
        RECT zoomedRect{};
        GetWindowRect(hwnd, &zoomedRect);
        const QMargins insets = window.contentsMargins();
        c.check(IsZoomed(hwnd) && window.isMaximized(), "maximizar con el boton",
                QStringLiteral("IsZoomed=%1 isMaximized=%2 rect=%3,%4 %5x%6 insets=%7,%8,%9,%10")
                    .arg(IsZoomed(hwnd)).arg(window.isMaximized())
                    .arg(zoomedRect.left).arg(zoomedRect.top).arg(zoomedRect.right - zoomedRect.left)
                    .arg(zoomedRect.bottom - zoomedRect.top)
                    .arg(insets.left()).arg(insets.top()).arg(insets.right()).arg(insets.bottom()));
    }
    c.check(bar->maximizeButton()->isMaximizedGlyph(), "dibujo de restaurar al maximizar");
    const QScreen *screen = window.screen();
    const QRect work = screen ? screen->availableGeometry() : QRect();
    const QRect barGlobal(bar->mapToGlobal(QPoint(0, 0)), bar->size());
    c.check(qAbs(barGlobal.left() - work.left()) <= 1 && qAbs(barGlobal.top() - work.top()) <= 1
                && qAbs(barGlobal.right() - work.right()) <= 1,
            "maximizada, la barra coincide con el area de trabajo",
            QStringLiteral("bar=%1,%2 %3 px work=%4,%5 %6 px")
                .arg(barGlobal.left()).arg(barGlobal.top()).arg(barGlobal.width())
                .arg(work.left()).arg(work.top()).arg(work.width()));
    c.check(hitAt(hwnd, &window, window.mapFromGlobal(QPoint(work.center().x(), work.top() + 2))) == HTCAPTION,
            "maximizada, el borde de arriba arrastra (sin resize)");
    c.check(hitAt(hwnd, &window, window.mapFromGlobal(QPoint(work.left() + 2, work.center().y()))) == HTCLIENT,
            "maximizada, el borde izquierdo no redimensiona");

    // Restaurar, minimizar y cerrar con los botones de la barra.
    press(bar->maximizeButton());
    settle(600);
    c.check(!IsZoomed(hwnd) && !window.isMaximized() && !bar->maximizeButton()->isMaximizedGlyph(), "restaurar con el boton");
    c.check(window.geometry() == normalBefore && window.contentsMargins().isNull(), "restaurada vuelve a su rectangulo, sin margenes");
    press(bar->minimizeButton());
    settle(600);
    c.check(IsIconic(hwnd), "minimizar con el boton");
    window.showNormal();
    settle(400);
    press(bar->closeButton());
    settle(300);
    c.check(!window.isVisible(), "cerrar con el boton");

    fprintf(stdout, "titlebar-qa result=%s checks=%d/%d platform=%s dpr=%.2f\n", c.passed == c.total ? "ok" : "FAIL",
            c.passed, c.total, qPrintable(QGuiApplication::platformName()), window.devicePixelRatioF());
    return c.passed == c.total ? 0 : 1;
#endif
}

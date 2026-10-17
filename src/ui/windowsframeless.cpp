// Copiado de LGA_RotoWorker (src/ui/shell/WindowsFrameless.cpp), que lo trae de LGA_FaceLock y este de
// LGA_MediaTools_v2 (apps/player/src/WindowsFrameless.cpp). Logica sin cambios: solo el namespace pasa a
// VdWindowsFrameless. Al final van los dos resguardos del marco que RotoWorker sumo desde FaceLock.

#include "videodownloader/windowsframeless.h"

#ifdef Q_OS_WIN

#include <QMargins>
#include <QPoint>
#include <QWidget>

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

namespace {

// Ancho de la banda sensible al resize, en px LOGICOS. 8 px es lo que usa Windows para el
// borde de una ventana con marco; se convierte a px fisicos con el devicePixelRatio de la
// ventana (no con GetSystemMetrics, que devuelve el DPI del proceso y miente en multi-monitor
// con escalas distintas).
constexpr int kResizeBorderLogical = 8;

int resizeBorderDevicePx(const QWidget *window)
{
    const qreal dpr = window ? window->devicePixelRatioF() : 1.0;
    return qMax(1, qRound(kResizeBorderLogical * dpr));
}

} // namespace

namespace VdWindowsFrameless {

// Techo del inset por lado, en px logicos.
constexpr int kMaxContentInset = 64;

QMargins maximizedContentInsets(const QWidget *window)
{
    if (!window || !window->internalWinId()) {
        return {};
    }
    HWND hwnd = reinterpret_cast<HWND>(window->internalWinId());
    // IsIconic ANTES que IsZoomed: minimizada, GetWindowRect devuelve el rect fuera de
    // pantalla (-32000,-32000) y de ahi saldrian margenes absurdos. En la practica Windows
    // limpia WS_MAXIMIZE al minimizar (medido: zoomed=false, iconic=true), pero el guard
    // cuesta nada y este calculo no puede depender de ese detalle.
    if (IsIconic(hwnd) || !IsZoomed(hwnd)) {
        return {};
    }
    RECT windowRect{};
    if (!GetWindowRect(hwnd, &windowRect)) {
        return {};
    }
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) {
        return {};
    }
    const RECT work = info.rcWork;
    // Diferencia fisica entre la ventana zoomed (que desborda el monitor) y el area visible,
    // pasada a px logicos. Con escalas no enteras puede haber 1 px de redondeo: preferible a
    // perder contenido, se redondea al entero mas cercano.
    const qreal dpr = window->devicePixelRatioF() > 0.0 ? window->devicePixelRatioF() : 1.0;
    // Cota superior: lo que se compensa es el borde de resize que Windows agrega al maximizar,
    // del orden de 8 px. Un rect anomalo (transiciones de arranque, cambios de monitor a medio
    // aplicar) no puede convertirse en un margen gigante que empuje el minimo del layout y
    // termine redimensionando la ventana.
    const auto inset = [dpr](LONG physical) {
        return qBound(0, qRound(physical / dpr), kMaxContentInset);
    };
    return QMargins(
        inset(work.left - windowRect.left),
        inset(work.top - windowRect.top),
        inset(windowRect.right - work.right),
        inset(windowRect.bottom - work.bottom));
}

void applyTo(QWidget *window)
{
    if (!window) {
        return;
    }
    window->createWinId();
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return;
    }

    // Por que Qt::FramelessWindowHint Y ADEMAS devolver el estilo nativo a mano:
    //
    // - Con el hint puesto, Qt crea la ventana como WS_POPUP. Un WS_POPUP no tiene snap, ni
    //   Win+flechas, ni Aero shake, ni animacion de minimizado. Por eso se le devuelve
    //   WS_OVERLAPPEDWINDOW: el que decide todo eso es el estilo REAL del HWND, no lo que Qt
    //   crea que es la ventana. El marco no vuelve a aparecer porque lo saca WM_NCCALCSIZE.
    // - El hint queda puesto igual del lado de Qt A PROPOSITO. Sin el,
    //   QWindowsWindow::calculateFullFrameMargins() (Qt 6.5, fix de QTBUG-113736) recalcula
    //   los margenes con AdjustWindowRectExForDpi y, viendo que la ventana y el cliente miden
    //   lo mismo, concluye margenes (8, -8, 8, 8). Con eso Qt reporta la geometria 16 px mas
    //   angosta en cada WM_MOVE y el contenido queda cortado. Con el hint, esa funcion sale
    //   temprano y los margenes quedan en cero, que es la verdad.
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style = (style & ~static_cast<LONG_PTR>(WS_POPUP)) | WS_OVERLAPPEDWINDOW;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);

    SetWindowPos(
        hwnd,
        nullptr,
        0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
            | SWP_NOOWNERZORDER);

    // Esquinas redondeadas (Windows 11). Las hace el compositor al componer la ventana, que
    // es un paso que ya ocurre para cualquier ventana: NO cuesta un repintado extra a la app
    // ni obliga a fondo translucido, que si romperia el camino de GPU del viewport.
    const DWORD kRoundPreference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(
        hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &kRoundPreference, sizeof(kRoundPreference));
}

bool handleNativeEvent(
    QWidget *window,
    const QByteArray &eventType,
    void *message,
    qintptr *result,
    const DragAreaProbe &isDragArea)
{
    if (!window || !result || eventType != QByteArrayLiteral("windows_generic_MSG")) {
        return false;
    }
    MSG *msg = static_cast<MSG *>(message);
    if (!msg || !msg->hwnd) {
        return false;
    }
    // Solo la ventana top-level propia: nativeEvent tambien ve mensajes de otros HWND.
    if (!window->internalWinId() || reinterpret_cast<HWND>(window->internalWinId()) != msg->hwnd) {
        return false;
    }
    HWND hwnd = msg->hwnd;

    switch (msg->message) {
    case WM_NCCALCSIZE: {
        if (msg->wParam == FALSE) {
            *result = 0;
            return true;
        }
        // rgrc[0] queda tal cual: el area cliente pasa a ser la ventana entera, sin marco ni
        // title bar nativos. Maximizada, la ventana zoomed desborda el monitor ~8 px por lado
        // (comportamiento de Windows imposible de corregir, ver maximizedContentInsets); se
        // deja el cliente igual a la ventana A PROPOSITO para que la geometria de Qt coincida
        // con la nativa, y el contenido se corre con maximizedContentInsets() desde la ventana.
        // WM_GETMINMAXINFO NO se toca: sus ptMaxPosition/ptMaxSize se ignoran para una ventana
        // WS_OVERLAPPEDWINDOW zoomed (medido), y al dejarlo pasar Qt aplica solo el tamano
        // minimo de la ventana con DPI correcto.
        *result = 0;
        return true;
    }

    case WM_NCHITTEST: {
        // En pantalla completa no hay barra ni bordes: que resuelva Qt.
        if (window->windowState() & Qt::WindowFullScreen) {
            return false;
        }

        const POINT screenPoint{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};

        if (!IsZoomed(hwnd)) {
            RECT windowRect{};
            if (GetWindowRect(hwnd, &windowRect)) {
                const int border = resizeBorderDevicePx(window);
                const bool onLeft = screenPoint.x < windowRect.left + border;
                const bool onRight = screenPoint.x >= windowRect.right - border;
                const bool onTop = screenPoint.y < windowRect.top + border;
                const bool onBottom = screenPoint.y >= windowRect.bottom - border;

                LRESULT hit = 0;
                if (onTop && onLeft)          hit = HTTOPLEFT;
                else if (onTop && onRight)    hit = HTTOPRIGHT;
                else if (onBottom && onLeft)  hit = HTBOTTOMLEFT;
                else if (onBottom && onRight) hit = HTBOTTOMRIGHT;
                else if (onLeft)              hit = HTLEFT;
                else if (onRight)             hit = HTRIGHT;
                else if (onTop)               hit = HTTOP;
                else if (onBottom)            hit = HTBOTTOM;

                if (hit != 0) {
                    *result = hit;
                    return true;
                }
            }
        }

        POINT clientPoint = screenPoint;
        ScreenToClient(hwnd, &clientPoint);
        const qreal dpr = window->devicePixelRatioF() > 0.0 ? window->devicePixelRatioF() : 1.0;
        const QPoint logicalPoint(
            qRound(static_cast<qreal>(clientPoint.x) / dpr),
            qRound(static_cast<qreal>(clientPoint.y) / dpr));

        if (isDragArea && isDragArea(logicalPoint)) {
            // HTCAPTION: el arrastre, el snap contra los bordes, el doble click para
            // maximizar y el menu de sistema con boton derecho los sigue haciendo Windows.
            *result = HTCAPTION;
            return true;
        }
        *result = HTCLIENT;
        return true;
    }

    default:
        break;
    }

    return false;
}

// --- Resguardos del marco: copia de handleNcActivate() y refreshFrame() de StartupPaintProbe.cpp de
// FaceLock, con su ownHwnd() escrito en linea. ---

bool handleNcActivate(const QWidget *window, void *message, qintptr *result)
{
    const HWND own = window && window->internalWinId() ? reinterpret_cast<HWND>(window->internalWinId()) : nullptr;
    MSG *msg = static_cast<MSG *>(message);
    if (!msg || !result || msg->message != WM_NCACTIVATE || !msg->hwnd || msg->hwnd != own) return false;
    // lParam = -1: DefWindowProc cambia el estado de activacion igual que siempre pero no repinta el area no-cliente.
    // Con el marco sacado por WM_NCCALCSIZE, ese repintado es el titulo clasico dibujado encima de la ventana.
    *result = DefWindowProcW(msg->hwnd, WM_NCACTIVATE, msg->wParam, -1);
    return true;
}

void refreshFrame(QWidget *window)
{
    const HWND hwnd = window && window->internalWinId() ? reinterpret_cast<HWND>(window->internalWinId()) : nullptr;
    if (!hwnd) return;
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

bool toggleMaximized(QWidget *window)
{
    const HWND hwnd = window && window->internalWinId() ? reinterpret_cast<HWND>(window->internalWinId()) : nullptr;
    if (!hwnd) return false;
    // Qt se entera por WM_SIZE (SIZE_MAXIMIZED / SIZE_RESTORED) y actualiza windowState() solo.
    ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    return true;
}

} // namespace VdWindowsFrameless

#endif // Q_OS_WIN

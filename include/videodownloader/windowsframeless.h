#ifndef WINDOWSFRAMELESS_H
#define WINDOWSFRAMELESS_H

// Copiado de LGA_RotoWorker (include/rotoworker/ui/shell/WindowsFrameless.h), que lo trae de LGA_FaceLock y
// este de LGA_MediaTools_v2 (Shot Player). Sin cambios de comportamiento: solo el include guard y el namespace.
// Como se usa: MainWindow (mainwindow.cpp, initWindowFrame y los eventos de ventana).

#include <QtGlobal>

#ifdef Q_OS_WIN

#include <QByteArray>
#include <QMargins>

#include <functional>

class QPoint;
class QWidget;

// Marco propio en Windows SIN perder la ventana nativa.
//
// Se combinan DOS cosas, y las dos hacen falta (el detalle y el porque, en applyTo()):
//   - Qt::FramelessWindowHint del lado de Qt, para que sus margenes de marco queden en cero.
//   - El estilo nativo WS_OVERLAPPEDWINDOW repuesto a mano sobre el HWND, que es lo que le
//     devuelve snap, Win+flechas, Aero shake, animacion de minimizado y menu de sistema.
// El dibujo del marco lo saca WM_NCCALCSIZE.
namespace VdWindowsFrameless {

/// Devuelve true si el punto —coordenadas LOGICAS del area cliente de la ventana— es zona de
/// arrastre (la barra propia menos sus botones).
using DragAreaProbe = std::function<bool(const QPoint &)>;

/// Fuerza el recalculo del area no-cliente sobre una ventana ya creada.
void applyTo(QWidget *window);

/// Margenes LOGICOS que el contenido debe dejar cuando la ventana esta maximizada.
///
/// Windows posiciona una ventana zoomed con WS_THICKFRAME en el area de trabajo INFLADA por
/// el marco invisible (~8 px fisicos por lado, fuera del monitor): medido en la app original,
/// -8,-8 con 3456x1408 sobre un monitor de 3440x1440. Los ptMaxPosition/ptMaxSize de
/// WM_GETMINMAXINFO se escriben bien y Windows los ignora.
///
/// En vez de pelear con el rect de la ventana, se corre el CONTENIDO hacia adentro. La razon
/// de fondo es que la geometria que Qt cree tener sale de GetWindowRect: si se achicara el
/// area cliente en WM_NCCALCSIZE, Qt seguiria creyendo que mide la ventana entera y tanto el
/// layout como mapToGlobal quedarian corridos (probado en el original: rompe el resize y
/// descuelga los popups). Corriendo el contenido, cliente == ventana y no hay nada que
/// sincronizar.
///
/// Se lee del rect VIVO en cada cambio de estado y de tamano, asi que el mecanismo converge
/// aunque Qt reescriba el rect de la ventana por su cuenta (lo hace en handleResized para
/// ventanas frameless zoomed): si el rect ya coincide con el area de trabajo, estos margenes
/// dan cero y no se aplica nada.
/// Devuelve margenes nulos si la ventana no esta maximizada.
QMargins maximizedContentInsets(const QWidget *window);

/// Se llama desde QWidget::nativeEvent. Devuelve true si consumio el mensaje.
bool handleNativeEvent(
    QWidget *window,
    const QByteArray &eventType,
    void *message,
    qintptr *result,
    const DragAreaProbe &isDragArea);

// --- Resguardos del marco (FaceLock: StartupPaintProbe, D-242) ---

/// true si consumio WM_NCACTIVATE de `window`, contestado con lParam = -1: la ventana se activa o
/// desactiva igual, pero Windows no repinta un titulo clasico encima de la barra propia al cambiar el foco.
bool handleNcActivate(const QWidget *window, void *message, qintptr *result);

/// Repinta marco y cliente de la ventana ya creada, para que el primer ciclo de marco y pintado
/// despues de applyTo() no dependa de un evento de entrada.
void refreshFrame(QWidget *window);

// --- Propio de VideoDownloader ---

/// Maximiza o restaura la ventana POR WINDOWS (ShowWindow), el mismo camino que el doble clic en la
/// barra y Win+flechas. Qt 6.8 (este repo; FaceLock y RotoWorker usan 6.5) maximiza una ventana con
/// Qt::FramelessWindowHint acomodando su rectangulo al area de trabajo sin avisarle a Windows
/// (medido con --qa-titlebar: IsZoomed=0 con la ventana "maximizada"), y asi quedaban los bordes de
/// resize activos y arrastrar la barra no la restauraba. Devuelve false si no hay HWND.
bool toggleMaximized(QWidget *window);

} // namespace VdWindowsFrameless

#endif // Q_OS_WIN

#endif // WINDOWSFRAMELESS_H

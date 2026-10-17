#ifndef APPHEADERBAR_H
#define APPHEADERBAR_H

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QPushButton;
class VdBarIconButton;
class VdWindowButton;

// Barra de titulo PROPIA de la ventana (sin barra nativa en Windows), la de FaceLock y RotoWorker
// (copiada de AppHeaderBar de LGA_RotoWorker, docs/Doc_Ventana_y_TitleBar.md alla): icono de la app,
// nombre, los avisos (tools, update), Settings, Help, un separador y los botones de ventana
// (minimizar, maximizar/restaurar, cerrar). Alto 43 + linea inferior de 1 px.
//
// Zona de arrastre de la ventana: el icono, el nombre y el hueco del medio. El grupo de la derecha
// entero NO arrastra, desde su primer control visible (un aviso o Settings) hasta el borde, huecos y
// separador incluidos: un doble clic que erra a Settings por unos pixeles no maximiza la ventana.
class AppHeaderBar : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kBarHeight = 43;

    explicit AppHeaderBar(QWidget *parent = nullptr);

    // tone: "neutral", "run" o "err". Texto vacio oculta el aviso.
    void setToolsNotice(const QString &text, const QString &tone, const QString &tooltip);
    void setUpdateNotice(const QString &text);

    // El dialogo de Settings o de Help esta abierto: su icono queda en el gris de «cursor encima».
    void setSettingsActive(bool active);
    void setHelpActive(bool active);

    // --- Ventana sin marco nativo (mismos nombres que en FaceLock y RotoWorker) ---
    // Fuera de Windows el marco es el del sistema y los tres botones sobran.
    void setWindowButtonsVisible(bool visible);
    // Cambia el dibujo del boton del medio entre maximizar y restaurar.
    void setWindowMaximized(bool maximized);
    // True si el punto (coordenadas de esta barra) es zona de arrastre de la ventana.
    bool isDragAreaAt(const QPoint &posInBar) const;

    VdBarIconButton *settingsButton() const { return m_settingsButton; }
    VdBarIconButton *helpButton() const { return m_helpButton; }
    VdWindowButton *minimizeButton() const { return m_minimizeButton; }
    VdWindowButton *maximizeButton() const { return m_maximizeButton; }
    VdWindowButton *closeButton() const { return m_closeButton; }

signals:
    void settingsClicked();
    void helpClicked();
    void toolsNoticeClicked();
    void updateNoticeClicked();
    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QHBoxLayout *m_rowLayout = nullptr;
    QPushButton *m_toolsNotice = nullptr;
    QPushButton *m_updateNotice = nullptr;
    VdBarIconButton *m_settingsButton = nullptr;
    VdBarIconButton *m_helpButton = nullptr;
    QWidget *m_windowControls = nullptr;
    VdWindowButton *m_minimizeButton = nullptr;
    VdWindowButton *m_maximizeButton = nullptr;
    VdWindowButton *m_closeButton = nullptr;
};

#endif // APPHEADERBAR_H

#ifndef HELPDIALOG_H
#define HELPDIALOG_H

#include <QDialog>

class QBoxLayout;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

// Estado del update tal como lo muestra Settings. Lo arma MainWindow con lo que informa el
// LgaUpdateService (o la captura de QA con datos de prueba): el dialogo no conoce el servicio.
// La descarga, la instalacion y sus errores los muestran los carteles de lgaupdate, no Settings.
struct UpdateView {
    QString availableVersion;  // version nueva que encontro un chequeo; vacio = ninguna
    bool checking = false;
    QString blockedReason;     // build de desarrollo: esta copia no se actualiza
};

// Marco comun de Help y Settings: sin marco del sistema, caja redondeada propia sobre un velo que
// oscurece la ventana, titulo con la X arriba y Close abajo. Cada dialogo arma su contenido entre
// los dos con los ayudantes de aca.
class PanelDialog : public QDialog
{
    Q_OBJECT
public:
    // Abre el dialogo modal centrado sobre la ventana, con el velo detras.
    int execOver(QWidget *window);

protected:
    explicit PanelDialog(const QString &objectName, const QString &windowTitle, QWidget *parent);

    // Fila del titulo (el dialogo le agrega sus textos; la X va sola al final, en finish()).
    QHBoxLayout *titleRow() const { return m_titleRow; }
    // Cuerpo entre el titulo y el pie.
    QVBoxLayout *body() const { return m_body; }
    // Cierra el armado: la X del titulo y el pie con Close. Va al final del constructor.
    void finish();
    QPushButton *closeButton() const { return m_closeButton; }

    // Alto del dialogo segun el layout ya pulido.
    void fitHeight();

    QLabel *label(const QString &text, const char *name);
    QLabel *linkLabel(const QString &url, const QString &text);
    QHBoxLayout *keyValueRow(const QString &key, const char *keyName);
    void addRule(QBoxLayout *layout);
    static QString link(const QString &url, const QString &text);

    void paintEvent(QPaintEvent *event) override;

private:
    QHBoxLayout *m_titleRow = nullptr;
    QVBoxLayout *m_body = nullptr;
    QPushButton *m_closeButton = nullptr;
};

// Help: version, What's new, autor, que es la app y creditos de las herramientas.
class HelpDialog : public PanelDialog
{
    Q_OBJECT
public:
    explicit HelpDialog(QWidget *parent = nullptr);

signals:
    // El link "What's new": el historial completo, en el cartel de lgaupdate.
    void whatsNewRequested();
};

// Velo semitransparente sobre la ventana mientras hay un dialogo abierto.
class Scrim : public QWidget
{
    Q_OBJECT
public:
    explicit Scrim(QWidget *parent);
protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif // HELPDIALOG_H

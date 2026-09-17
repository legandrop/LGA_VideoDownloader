#ifndef UIWIDGETS_H
#define UIWIDGETS_H

#include <QComboBox>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QWidget>

class QPainter;
class QPushButton;

// Piezas chicas compartidas por las tarjetas: iconos vectoriales, chips, barra de progreso,
// insignia de estado y label con elipsis. Todo se pinta con QPainter a la escala real del
// dispositivo, sin mapas de bits reescalados.

enum class Icon {
    Help, Folder, X, Retry, Check, Alert, Clock, Copy, Cookie, Link, External, ArrowDown
};

namespace Icons {
// Pinta el icono dentro de rect (se escala desde su viewBox original).
void paint(QPainter &painter, Icon icon, const QRectF &rect, const QColor &color);
// QIcon vectorial para botones; el color disabled se atenua solo.
QIcon icon(Icon icon, const QColor &color);
} // namespace Icons

// Icono suelto como widget (para poner al lado de un texto).
class IconWidget : public QWidget
{
    Q_OBJECT
public:
    IconWidget(Icon icon, const QColor &color, int size, QWidget *parent = nullptr);
    void setIcon(Icon icon, const QColor &color);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    Icon m_icon;
    QColor m_color;
};

// Label de una linea que recorta con "..." en vez de empujar el layout.
class ElidedLabel : public QWidget
{
    Q_OBJECT
public:
    explicit ElidedLabel(QWidget *parent = nullptr);
    void setText(const QString &text);
    QString text() const { return m_text; }
    void setElideMode(Qt::TextElideMode mode);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QString m_text;
    Qt::TextElideMode m_mode = Qt::ElideRight;
};

// Chip de estado: tono (ok, err, warn, run, src o neutro) + icono opcional + texto.
class Chip : public QFrame
{
    Q_OBJECT
public:
    explicit Chip(QWidget *parent = nullptr);
    void set(const QString &tone, const QString &text, bool hasIcon = false, Icon icon = Icon::Check);
private:
    IconWidget *m_icon;
    QLabel *m_label;
    QString m_tone;
};

// Barra de progreso fina pintada a mano (QProgressBar con QSS pierde el radio del relleno).
class ProgressLine : public QWidget
{
    Q_OBJECT
public:
    enum class Tone { Active, Done, Idle };
    explicit ProgressLine(int height = 8, QWidget *parent = nullptr);
    void setValue(double fraction, Tone tone);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    double m_fraction = 0.0;
    Tone m_tone = Tone::Idle;
};

// Circulo de 28px con el icono del estado de un item de la cola.
class StatusBadge : public QWidget
{
    Q_OBJECT
public:
    enum class Kind { Done, Running, Waiting, Error };
    explicit StatusBadge(QWidget *parent = nullptr);
    void setKind(Kind kind);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    Kind m_kind = Kind::Waiting;
};

// Combo con el triangulo del diseno LGA (la flecha nativa se oculta por QSS).
class ArrowComboBox : public QComboBox
{
    Q_OBJECT
public:
    explicit ArrowComboBox(QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *event) override;
};

// Ayudas para construir botones con las variantes de la hoja de estilo.
namespace Ui {
QPushButton *button(const QString &text, const QString &variant = QString(), const QString &size = QString(),
                    QWidget *parent = nullptr);
void setIcon(QPushButton *button, Icon icon, const QColor &color, int size = 14);
void repolish(QWidget *widget);
} // namespace Ui

#endif // UIWIDGETS_H

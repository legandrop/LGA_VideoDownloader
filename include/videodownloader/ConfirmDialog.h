#ifndef CONFIRMDIALOG_H
#define CONFIRMDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>

// Cartel de los carteles del auto-update (`libs/lgaupdate`): version nueva, notas, resultado de un
// chequeo. La biblioteca lo pide por este nombre y con esta API, la misma que el ConfirmDialog de
// ThetaExplorer: cada app que usa lgaupdate aporta el suyo con el aspecto de sus dialogos. Aca es
// el de Help y What's new: caja redondeada propia, sin marco del sistema, sobre el velo que
// oscurece la ventana.
class ConfirmDialog : public QDialog
{
    Q_OBJECT
public:
    enum class ButtonStyle { Neutral, Action, Danger };

    struct Button {
        QString     text;
        ButtonStyle style = ButtonStyle::Neutral;
        Qt::Key     shortcut = Qt::Key_unknown;
    };

    struct Spec {
        QString       iconName;      // glyph del cartel en ThetaExplorer; esta app no lleva icono
        QString       title;
        QString       subtitle;      // texto enriquecido (<b>, <br>)
        QString       hintHtml;      // fila de atajos; las teclas van en <b>
        QString       contentHtml;   // notas opcionales, con scroll
        QString       details;       // detalle tecnico plano, inicialmente oculto
        QList<Button> buttons;       // de izquierda a derecha
        int           defaultIndex = -1;   // el que responde a Enter
        int           escapeIndex  = -1;   // lo que devuelve Esc o cerrar la ventana
        QList<int>    noFocusIndexes;      // botones que solo responden al click (destructivos)
    };

    // Devuelve el indice del boton apretado, o spec.escapeIndex.
    static int ask(QWidget *parent, const Spec &spec);

    // Arma el cartel sin abrirlo, para la captura de QA (`--ui-shot`).
    static ConfirmDialog *build(const Spec &spec, QWidget *parent);
    // Alto segun el contenido y el ancho fijo. Publico para la captura de QA.
    void fitHeight();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    ConfirmDialog(const Spec &spec, QWidget *parent);
    int m_result = -1;
};

#endif // CONFIRMDIALOG_H

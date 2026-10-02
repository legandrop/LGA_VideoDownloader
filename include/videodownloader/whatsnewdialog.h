#ifndef WHATSNEWDIALOG_H
#define WHATSNEWDIALOG_H

#include <QDialog>

class QTextBrowser;

// Bloque de notas (What's new) que comparten este dialogo y Help: texto con scroll propio y alto
// acotado, para que un historial largo no estire la ventana.
namespace NotesView {
QTextBrowser *create(QWidget *parent);
// Carga el HTML y fija el alto: el del texto para `width`, nunca mas que `maxHeight`.
void setHtml(QTextBrowser *view, const QString &html, int width, int maxHeight);
} // namespace NotesView

// Notas de la version recien instalada, una sola vez despues de un update que el usuario no vio
// venir (instalador a mano, otra persona). Mismo estilo que Help: caja propia sobre el velo.
class WhatsNewDialog : public QDialog
{
    Q_OBJECT
public:
    WhatsNewDialog(const QString &version, const QString &notesHtml, QWidget *parent = nullptr);

    // Modal, centrado sobre la ventana, con el velo detras.
    int execOver(QWidget *window);
    // Alto segun el contenido, con el bloque de notas acotado. Publico para la captura de QA.
    void fitHeight();

protected:
    void paintEvent(QPaintEvent *event) override;
};

#endif // WHATSNEWDIALOG_H

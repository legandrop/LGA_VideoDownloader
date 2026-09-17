#ifndef TABHEADER_H
#define TABHEADER_H

#include <QWidget>

class QPushButton;

// Barra de pestanas LGA de 50px: pestana DOWNLOADS, avisos discretos (tools, update) y el
// boton de ayuda en la esquina.
class TabHeader : public QWidget
{
    Q_OBJECT
public:
    explicit TabHeader(QWidget *parent = nullptr);

    // tone: "neutral", "run" o "err". Texto vacio oculta el aviso.
    void setToolsNotice(const QString &text, const QString &tone, const QString &tooltip);
    void setUpdateNotice(const QString &text);

signals:
    void helpClicked();
    void toolsNoticeClicked();
    void updateNoticeClicked();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPushButton *m_toolsNotice;
    QPushButton *m_updateNotice;
};

#endif // TABHEADER_H

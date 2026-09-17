#ifndef ADDVIDEOSCARD_H
#define ADDVIDEOSCARD_H

#include <QFrame>

#include "browserdetect.h"
#include "downloaditem.h"

class ArrowComboBox;
class ElidedLabel;
class QLabel;
class QPlainTextEdit;
class QPushButton;

// Tarjeta "Add videos": links, formato, calidad, carpeta, origen de la sesion y Download.
// No guarda nada: MainWindow lee y persiste los valores.
class AddVideosCard : public QFrame
{
    Q_OBJECT
public:
    explicit AddVideosCard(QWidget *parent = nullptr);

    // Links pegados, uno por renglon, sin vacios.
    QStringList links() const;
    void setLinksText(const QString &text);
    void clearLinks();

    void setDownloadFolder(const QString &path);
    void setBrowsers(const QList<BrowserDetect::Browser> &browsers);
    // key: "" = ninguno, "file" = cookies.txt, o la clave del navegador.
    void setCookiesSource(const QString &key, const QString &cookiesFile);
    QString cookiesSource() const;
    // Resalta "Use cookies from" en rojo cuando un video fallo por falta de sesion.
    void setCookiesAttention(bool attention);

    void setFormat(OutputFormat format);
    OutputFormat format() const;
    void setQuality(VideoQuality quality);
    VideoQuality quality() const;

signals:
    void downloadRequested();
    void browseRequested();
    void cookiesSourceActivated(const QString &key);
    void formatChanged(OutputFormat format);
    void qualityChanged(VideoQuality quality);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateNote();

    QPlainTextEdit *m_links;
    QPushButton *m_download;
    ArrowComboBox *m_format;
    ArrowComboBox *m_quality;
    QFrame *m_folderField;
    ElidedLabel *m_folder;
    QPushButton *m_browse;
    ArrowComboBox *m_cookies;
    QLabel *m_note;
    QList<BrowserDetect::Browser> m_browsers;
};

#endif // ADDVIDEOSCARD_H

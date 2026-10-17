#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "helpdialog.h"

#include <QMap>

class QLabel;
class QPushButton;
class QWidget;

// Settings: actualizaciones de la app, extension de navegador y versiones de las tools. Mismo marco
// que Help (PanelDialog). Lo que se elige por descarga (formato, calidad, carpeta, sesion del
// navegador) sigue en la tarjeta Add videos.
class SettingsDialog : public PanelDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void setUpdateView(const UpdateView &view);
    void setToolVersions(const QMap<QString, QString> &versions);

signals:
    void checkRequested();
    void installRequested();
    void openExtensionFolderRequested();

private:
    QLabel *m_dot;
    QLabel *m_updateTitle;
    QLabel *m_updateCaption;
    QLabel *m_checkStatus;
    QPushButton *m_check;
    QPushButton *m_install;
    QPushButton *m_later;
    QLabel *m_ytdlpVersion;
    QLabel *m_ffmpegVersion;
    QLabel *m_denoVersion;
    QWidget *m_updateBox;
};

#endif // SETTINGSDIALOG_H

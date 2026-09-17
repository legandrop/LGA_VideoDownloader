#ifndef HELPDIALOG_H
#define HELPDIALOG_H

#include <QDateTime>
#include <QDialog>
#include <QMap>

#include "updateservice.h"

class QLabel;
class QProgressBar;
class QPushButton;

// Estado del update tal como lo muestra el dialogo. Lo arma MainWindow desde UpdateService
// (o la captura de QA con datos de prueba): el dialogo no conoce el servicio.
struct UpdateView {
    UpdateService::State state = UpdateService::State::Idle;
    QString currentVersion;
    QString availableVersion;
    QString error;
    QString blockedReason;     // build de desarrollo: no se puede instalar desde aca
    bool installsInPlace = true;
    qint64 received = -1;
    qint64 total = -1;
    QDateTime lastChecked;
};

// Dialogo de ayuda: version, actualizaciones, extension de navegador, versiones de las tools
// y creditos. Sin marco
// del sistema; se dibuja su propia caja redondeada sobre un velo que oscurece la ventana.
class HelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HelpDialog(QWidget *parent = nullptr);

    void setUpdateView(const UpdateView &view);
    void setToolVersions(const QMap<QString, QString> &versions);

    // Abre el dialogo modal centrado sobre la ventana, con el velo detras.
    int execOver(QWidget *window);

signals:
    void checkRequested();
    void installRequested();
    void cancelInstallRequested();
    void openExtensionFolderRequested();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    // Alto del dialogo segun el layout ya pulido (ver helpdialog.cpp).
    void fitHeight();

    QLabel *m_dot;
    QLabel *m_updateTitle;
    QLabel *m_updateCaption;
    QProgressBar *m_updateProgress;
    QLabel *m_checkStatus;
    QPushButton *m_check;
    QPushButton *m_install;
    QPushButton *m_later;
    QPushButton *m_cancelInstall;
    QLabel *m_ytdlpVersion;
    QLabel *m_ffmpegVersion;
    QLabel *m_denoVersion;
    QWidget *m_updateBox;
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

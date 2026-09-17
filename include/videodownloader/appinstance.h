#ifndef APPINSTANCE_H
#define APPINSTANCE_H

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>

#include <functional>

#include "nativehost.h"

class QLocalServer;
class QLocalSocket;
class QLockFile;

// Instancia unica de la app por usuario.
//
// El arbitro es un QLockFile en AppLocalDataLocation: en Windows dos QLocalServer pueden
// escuchar el mismo nombre a la vez, asi que el server solo NO excluye. Quien toma el lock es
// la instancia principal y es la unica que escucha. Una segunda apertura del exe manda
// "activate" y sale; el host de Native Messaging manda sus pedidos por el mismo socket.
class AppInstance : public QObject
{
    Q_OBJECT

public:
    using DownloadHandler = std::function<QJsonObject(const NativeHost::Request &request)>;
    using ActivateHandler = std::function<void()>;

    explicit AppInstance(QObject *parent = nullptr);
    ~AppInstance() override;

    // Nombre del server local: LGA_VideoDownloader_<usuario>.
    static QString serverName();

    // Toma el lock y empieza a escuchar. false = ya hay otra instancia principal.
    bool acquire();

    // Segunda instancia: pide a la principal que traiga su ventana al frente.
    static bool sendActivate(int timeoutMs);

    // Los pedidos que llegan antes de esto (la ventana todavia no existe) quedan en espera y se
    // procesan al registrar los handlers.
    void setHandlers(DownloadHandler download, ActivateHandler activate);

private:
    void onNewConnection();
    void onReadyRead(QLocalSocket *socket);
    void handleFrame(QLocalSocket *socket, const QByteArray &payload);
    void reply(QLocalSocket *socket, const QJsonObject &response);

    QLockFile *m_lock = nullptr;
    QLocalServer *m_server = nullptr;
    DownloadHandler m_download;
    ActivateHandler m_activate;
    QHash<QLocalSocket *, QByteArray> m_buffers;
    struct Pending {
        QPointer<QLocalSocket> socket;
        QByteArray payload;
    };
    QList<Pending> m_pending;
};

#endif // APPINSTANCE_H

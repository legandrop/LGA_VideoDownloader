#include "videodownloader/appinstance.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QtEndian>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// Un pedido del host trae como mucho 8 MB (el tope del contrato) mas el sobre.
constexpr quint32 MAX_FRAME_BYTES = quint32(NativeHost::kMaxMessageBytes + 64 * 1024);
// Una conexion que no completa su mensaje en este tiempo se corta.
constexpr int CONNECTION_TIMEOUT_MS = 10000;
// Un pedido que llega antes de que exista la ventana espera como mucho esto.
constexpr int PENDING_TIMEOUT_MS = 20000;

QJsonObject errorReply(const QString &code, const QString &message)
{
    return QJsonObject{{QStringLiteral("v"), NativeHost::kProtocolVersion}, {QStringLiteral("ok"), false},
                       {QStringLiteral("error"), code}, {QStringLiteral("message"), message}};
}

} // namespace

AppInstance::AppInstance(QObject *parent)
    : QObject(parent)
{
}

AppInstance::~AppInstance()
{
    if (m_server) {
        m_server->close();
    }
    if (m_lock) {
        m_lock->unlock();
        delete m_lock;
    }
}

QString AppInstance::serverName()
{
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty()) {
        user = qEnvironmentVariable("USER");
    }
    QString safe;
    for (const QChar ch : std::as_const(user)) {
        safe += (ch.isLetterOrNumber() && ch.unicode() < 128) || ch == QLatin1Char('_') || ch == QLatin1Char('-')
                    ? ch : QLatin1Char('_');
    }
    if (safe.isEmpty()) {
        safe = QStringLiteral("user");
    }
    return QStringLiteral("LGA_VideoDownloader_") + safe;
}

bool AppInstance::acquire()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    m_lock = new QLockFile(QDir(dir).filePath(QStringLiteral("instance.lock")));
    // tryLock(0) tambien limpia un lock huerfano (el PID que lo tenia ya no existe).
    if (!m_lock->tryLock(0)) {
        delete m_lock;
        m_lock = nullptr;
        return false;
    }

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &AppInstance::onNewConnection);
#ifdef Q_OS_UNIX
    // Socket de un cierre abrupto: con el lock tomado nadie mas lo esta usando.
    QLocalServer::removeServer(serverName());
#endif
    if (!m_server->listen(serverName())) {
        qWarning() << "AppInstance: no se pudo escuchar en" << serverName() << m_server->errorString();
    }
    return true;
}

bool AppInstance::sendActivate(int timeoutMs)
{
#ifdef Q_OS_WIN
    // La instancia nueva la abrio el usuario, asi que puede ceder el foco a la principal.
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    QElapsedTimer clock;
    clock.start();
    const QByteArray message = NativeHost::frame(QJsonObject{{QStringLiteral("v"), NativeHost::kProtocolVersion},
                                                             {QStringLiteral("type"), QStringLiteral("activate")}});
    // La principal puede haber tomado el lock y todavia no estar escuchando: se reintenta.
    while (clock.elapsed() < timeoutMs) {
        QLocalSocket socket;
        socket.connectToServer(serverName());
        if (socket.waitForConnected(500)) {
            socket.write(message);
            socket.waitForBytesWritten(1000);
            socket.waitForReadyRead(1000);
            socket.disconnectFromServer();
            return true;
        }
        QThread::msleep(200);
    }
    return false;
}

void AppInstance::setHandlers(DownloadHandler download, ActivateHandler activate)
{
    m_download = std::move(download);
    m_activate = std::move(activate);
    const QList<Pending> pending = m_pending;
    m_pending.clear();
    for (const Pending &entry : pending) {
        if (entry.socket) {
            handleFrame(entry.socket, entry.payload);
        }
    }
}

void AppInstance::onNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        m_buffers.insert(socket, QByteArray());
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
        QTimer::singleShot(m_download ? CONNECTION_TIMEOUT_MS : PENDING_TIMEOUT_MS + CONNECTION_TIMEOUT_MS, socket,
                           [socket]() { socket->abort(); });
        if (socket->bytesAvailable() > 0) {
            onReadyRead(socket);
        }
    }
}

void AppInstance::onReadyRead(QLocalSocket *socket)
{
    if (!m_buffers.contains(socket)) {
        return;
    }
    QByteArray &buffer = m_buffers[socket];
    buffer += socket->readAll();
    if (buffer.size() < 4) {
        return;
    }
    const quint32 length = qFromLittleEndian<quint32>(buffer.constData());
    if (length > MAX_FRAME_BYTES) {
        reply(socket, errorReply(QStringLiteral("bad_request"), QStringLiteral("The message is too large.")));
        m_buffers.remove(socket);
        return;
    }
    if (buffer.size() < qsizetype(4 + length)) {
        return;
    }
    const QByteArray payload = buffer.mid(4, qsizetype(length));
    // Un mensaje por conexion: lo que siga se ignora.
    m_buffers.remove(socket);
    if (!m_download) {
        m_pending.append({QPointer<QLocalSocket>(socket), payload});
        return;
    }
    handleFrame(socket, payload);
}

void AppInstance::handleFrame(QLocalSocket *socket, const QByteArray &payload)
{
    // El socket es otra frontera: se vuelve a validar todo, igual que en el host.
    NativeHost::Request request;
    QString code;
    QString message;
    if (!NativeHost::parseRequest(payload, true, &request, &code, &message)) {
        reply(socket, errorReply(code, message));
        return;
    }
    if (request.type == QLatin1String("activate")) {
        if (m_activate) {
            m_activate();
        }
        reply(socket, QJsonObject{{QStringLiteral("v"), NativeHost::kProtocolVersion}, {QStringLiteral("ok"), true},
                                  {QStringLiteral("status"), QStringLiteral("activated")}});
        return;
    }
    if (request.type == QLatin1String("ping")) {
        reply(socket, QJsonObject{{QStringLiteral("v"), NativeHost::kProtocolVersion}, {QStringLiteral("ok"), true},
                                  {QStringLiteral("status"), QStringLiteral("pong")}});
        return;
    }
    reply(socket, m_download(request));
}

void AppInstance::reply(QLocalSocket *socket, const QJsonObject &response)
{
    if (!socket || socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    socket->write(NativeHost::frame(response));
    socket->flush();
    // El cliente cierra cuando lee la respuesta; si no, el timeout de la conexion lo corta.
}

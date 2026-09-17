#include "videodownloader/nativehost.h"
#include "videodownloader/appinstance.h"
#include "videodownloader/hostregistration.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLocalSocket>
#include <QProcess>
#include <QThread>
#include <QUrl>
#include <QtEndian>

#include <cmath>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace {

// Tiempos del modo host. El host termina siempre antes de 25 s: 5 s como mucho para leer el
// pedido, conexion (con arranque de la app) hasta los 20 s y respuesta hasta los 24 s.
constexpr int STDIN_TIMEOUT_MS = 5000;
constexpr int CONNECT_DEADLINE_MS = 20000;
constexpr int TOTAL_DEADLINE_MS = 24000;
constexpr int RETRY_INTERVAL_MS = 200;
constexpr int PING_CONNECT_MS = 1000;
// La respuesta de la app por el socket es chica; cualquier cosa mayor es un error.
constexpr quint32 MAX_APP_REPLY_BYTES = 64 * 1024;

const QLatin1String kOriginPrefix("chrome-extension://");

// ------------------------------------------------------------------ E/S binaria cruda

// Windows: ReadFile/WriteFile sobre los handles estandar, sin la capa de texto del CRT (que
// convertiria \n en \r\n y corromperia el largo del mensaje).
bool readStdinExact(char *buffer, size_t size)
{
    size_t done = 0;
#ifdef Q_OS_WIN
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE) {
        return false;
    }
    while (done < size) {
        DWORD chunk = 0;
        const DWORD want = DWORD(qMin<size_t>(size - done, 1 << 20));
        if (!ReadFile(input, buffer + done, want, &chunk, nullptr) || chunk == 0) {
            return false;
        }
        done += chunk;
    }
#else
    while (done < size) {
        const ssize_t chunk = ::read(STDIN_FILENO, buffer + done, size - done);
        if (chunk < 0 && errno == EINTR) {
            continue;
        }
        if (chunk <= 0) {
            return false;
        }
        done += size_t(chunk);
    }
#endif
    return true;
}

bool writeStdout(const QByteArray &data)
{
    qsizetype done = 0;
#ifdef Q_OS_WIN
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE) {
        return false;
    }
    while (done < data.size()) {
        DWORD chunk = 0;
        if (!WriteFile(output, data.constData() + done, DWORD(data.size() - done), &chunk, nullptr) || chunk == 0) {
            return false;
        }
        done += chunk;
    }
    FlushFileBuffers(output);
#else
    while (done < data.size()) {
        const ssize_t chunk = ::write(STDOUT_FILENO, data.constData() + done, size_t(data.size() - done));
        if (chunk < 0 && errno == EINTR) {
            continue;
        }
        if (chunk <= 0) {
            return false;
        }
        done += chunk;
    }
#endif
    return true;
}

enum class ReadStatus { Ok, Timeout, Closed, TooLarge };

struct ReadState {
    std::mutex mutex;
    std::condition_variable ready;
    bool done = false;
    ReadStatus status = ReadStatus::Closed;
    QByteArray payload;
};

// Lee UN mensaje (largo UInt32 en orden nativo + JSON) con timeout. La lectura va en un hilo
// aparte porque ReadFile sobre un pipe no tiene timeout: si el navegador no manda nada, el hilo
// queda bloqueado y el proceso sale igual.
ReadStatus readMessage(QByteArray *payload)
{
    auto state = std::make_shared<ReadState>();
    std::thread([state]() {
        ReadStatus status = ReadStatus::Ok;
        QByteArray data;
        char header[4];
        if (!readStdinExact(header, sizeof(header))) {
            status = ReadStatus::Closed;
        } else {
            quint32 length = 0;
            std::memcpy(&length, header, sizeof(length));
            if (qint64(length) > NativeHost::kMaxMessageBytes) {
                status = ReadStatus::TooLarge;
            } else {
                data.resize(qsizetype(length));
                if (length > 0 && !readStdinExact(data.data(), length)) {
                    status = ReadStatus::Closed;
                }
            }
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        state->status = status;
        state->payload = data;
        state->done = true;
        state->ready.notify_all();
    }).detach();

    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->ready.wait_for(lock, std::chrono::milliseconds(STDIN_TIMEOUT_MS), [&state]() { return state->done; })) {
        return ReadStatus::Timeout;
    }
    *payload = state->payload;
    return state->status;
}

void respond(const QJsonObject &response)
{
    const QByteArray json = QJsonDocument(response).toJson(QJsonDocument::Compact);
    const quint32 length = quint32(json.size());
    QByteArray message(sizeof(length), Qt::Uninitialized);
    std::memcpy(message.data(), &length, sizeof(length));
    message += json;
    writeStdout(message);
}

QJsonObject baseResponse()
{
    QJsonObject response{{QStringLiteral("v"), NativeHost::kProtocolVersion},
                         {QStringLiteral("app"), QStringLiteral(VIDEODOWNLOADER_VERSION)}};
    const QString extension = HostRegistration::installedExtensionVersion();
    if (!extension.isEmpty()) {
        response.insert(QStringLiteral("extension"), extension.left(32));
    }
    return response;
}

void respondError(const QString &code, const QString &message)
{
    QJsonObject response = baseResponse();
    response.insert(QStringLiteral("ok"), false);
    response.insert(QStringLiteral("error"), code);
    response.insert(QStringLiteral("message"), message.left(300));
    respond(response);
}

bool fail(QString *errorCode, QString *message, const char *code, const char *text)
{
    if (errorCode) {
        *errorCode = QLatin1String(code);
    }
    if (message) {
        *message = QLatin1String(text);
    }
    return false;
}

bool hasControlChars(const QString &text)
{
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\t') || ch == QLatin1Char('\r') || ch == QLatin1Char('\n') || ch == QChar(0)) {
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------ Conexion con la app

bool connectToApp(QLocalSocket &socket, int timeoutMs)
{
    socket.abort();
    socket.connectToServer(AppInstance::serverName());
    return socket.waitForConnected(timeoutMs);
}

// Manda un mensaje por el socket y espera la respuesta hasta `deadlineMs` (medido con `clock`).
bool exchange(QLocalSocket &socket, const QJsonObject &message, const QElapsedTimer &clock, int deadlineMs,
              QJsonObject *reply)
{
    const auto remaining = [&clock, deadlineMs]() { return int(qMax<qint64>(0, deadlineMs - clock.elapsed())); };
    socket.write(NativeHost::frame(message));
    while (socket.bytesToWrite() > 0 && remaining() > 0) {
        if (!socket.waitForBytesWritten(qMin(remaining(), 500)) && socket.state() != QLocalSocket::ConnectedState) {
            return false;
        }
    }
    QByteArray buffer;
    while (true) {
        buffer += socket.readAll();
        if (buffer.size() >= 4) {
            const quint32 length = qFromLittleEndian<quint32>(buffer.constData());
            if (length > MAX_APP_REPLY_BYTES) {
                return false;
            }
            if (buffer.size() >= qsizetype(4 + length)) {
                const QJsonDocument doc = QJsonDocument::fromJson(buffer.mid(4, qsizetype(length)));
                if (!doc.isObject()) {
                    return false;
                }
                *reply = doc.object();
                return true;
            }
        }
        if (remaining() <= 0) {
            return false;
        }
        if (!socket.waitForReadyRead(qMin(remaining(), 250)) && socket.bytesAvailable() == 0
            && socket.state() != QLocalSocket::ConnectedState) {
            return false;
        }
    }
}

// Lanza la app separada del host (y, si se puede, fuera del Job del navegador, para que
// cerrar el navegador no la cierre).
bool launchApp()
{
#ifdef Q_OS_WIN
    const std::wstring exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    const std::wstring dir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath()).toStdWString();
    std::wstring commandLine = L"\"" + exe + L"\"";
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // bInheritHandles FALSE: la app no hereda los pipes de stdin/stdout del navegador.
    DWORD flags = CREATE_BREAKAWAY_FROM_JOB | CREATE_NEW_PROCESS_GROUP;
    BOOL ok = CreateProcessW(exe.c_str(), buffer.data(), nullptr, nullptr, FALSE, flags, nullptr, dir.c_str(),
                             &startup, &process);
    if (!ok && GetLastError() == ERROR_ACCESS_DENIED) {
        // El Job no permite salir: se lanza adentro igual.
        flags &= ~DWORD(CREATE_BREAKAWAY_FROM_JOB);
        ok = CreateProcessW(exe.c_str(), buffer.data(), nullptr, nullptr, FALSE, flags, nullptr, dir.c_str(),
                            &startup, &process);
    }
    if (!ok) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#elif defined(Q_OS_MACOS)
    // El ejecutable vive en <App>.app/Contents/MacOS: se abre el bundle con LaunchServices.
    QDir bundle(QCoreApplication::applicationDirPath());
    if (bundle.dirName() == QLatin1String("MacOS") && bundle.cdUp() && bundle.cdUp()
        && bundle.dirName().endsWith(QLatin1String(".app"))) {
        return QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-a"), bundle.absolutePath()});
    }
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
#else
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
#endif
}

} // namespace

namespace NativeHost {

QString extensionId()
{
    return QStringLiteral(LGA_EXTENSION_ID);
}

QString allowedOrigin()
{
    return kOriginPrefix + extensionId() + QLatin1Char('/');
}

bool parseRequest(const QByteArray &json, bool allowActivate, Request *out, QString *errorCode, QString *message)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return fail(errorCode, message, "bad_request", "The message is not a JSON object.");
    }
    const QJsonObject object = doc.object();

    const QJsonValue version = object.value(QStringLiteral("v"));
    if (!version.isDouble() || std::floor(version.toDouble()) != version.toDouble()) {
        return fail(errorCode, message, "bad_request", "Missing or invalid protocol version.");
    }
    if (version.toDouble() > kProtocolVersion) {
        return fail(errorCode, message, "unsupported_version", "This app supports protocol version 1.");
    }
    if (version.toDouble() != kProtocolVersion) {
        return fail(errorCode, message, "bad_request", "Missing or invalid protocol version.");
    }

    const QJsonValue type = object.value(QStringLiteral("type"));
    if (!type.isString()) {
        return fail(errorCode, message, "bad_request", "Missing message type.");
    }
    Request request;
    request.type = type.toString();
    if (request.type == QLatin1String("ping") || (allowActivate && request.type == QLatin1String("activate"))) {
        *out = request;
        return true;
    }
    if (request.type != QLatin1String("download")) {
        return fail(errorCode, message, "bad_request", "Unknown message type.");
    }

    // URL: solo http/https con host. QUrl en modo estricto rechaza espacios y caracteres sueltos.
    const QJsonValue url = object.value(QStringLiteral("url"));
    if (!url.isString() || url.toString().isEmpty() || url.toString().size() > kMaxUrlChars) {
        return fail(errorCode, message, "bad_request", "Invalid link.");
    }
    const QUrl parsed(url.toString(), QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        || parsed.host().isEmpty() || hasControlChars(url.toString())) {
        return fail(errorCode, message, "bad_request", "Only http and https links can be downloaded.");
    }
    request.url = url.toString();

    const QJsonValue title = object.value(QStringLiteral("title"));
    if (!title.isUndefined()) {
        if (!title.isString() || title.toString().size() > kMaxTitleChars) {
            return fail(errorCode, message, "bad_request", "Invalid title.");
        }
        request.title = title.toString();
    }
    const QJsonValue browser = object.value(QStringLiteral("browser"));
    if (!browser.isUndefined()) {
        if (!browser.isString() || browser.toString().size() > kMaxBrowserChars) {
            return fail(errorCode, message, "bad_request", "Invalid browser name.");
        }
        request.browser = browser.toString();
    }

    const QJsonValue cookies = object.value(QStringLiteral("cookies"));
    if (!cookies.isUndefined()) {
        if (!cookies.isArray() || cookies.toArray().size() > kMaxCookies) {
            return fail(errorCode, message, "bad_request", "Invalid cookie list.");
        }
        request.hasCookies = true;
        const QJsonArray list = cookies.toArray();
        for (const QJsonValue &entry : list) {
            if (!entry.isObject()) {
                return fail(errorCode, message, "bad_request", "Invalid cookie.");
            }
            const QJsonObject cookie = entry.toObject();
            QJsonObject clean;
            bool drop = false;
            for (const char *field : {"domain", "path", "name", "value"}) {
                const QJsonValue value = cookie.value(QLatin1String(field));
                if (!value.isString()) {
                    return fail(errorCode, message, "bad_request", "Invalid cookie.");
                }
                drop = drop || hasControlChars(value.toString());
                clean.insert(QLatin1String(field), value);
            }
            for (const char *field : {"hostOnly", "secure", "httpOnly"}) {
                const QJsonValue value = cookie.value(QLatin1String(field));
                if (!value.isUndefined() && !value.isBool()) {
                    return fail(errorCode, message, "bad_request", "Invalid cookie.");
                }
                clean.insert(QLatin1String(field), value.toBool(false));
            }
            const QJsonValue expiration = cookie.value(QStringLiteral("expirationDate"));
            if (!expiration.isUndefined()) {
                if (!expiration.isDouble() || !std::isfinite(expiration.toDouble())) {
                    return fail(errorCode, message, "bad_request", "Invalid cookie.");
                }
                clean.insert(QStringLiteral("expirationDate"), expiration);
            }
            QString domain = cookie.value(QStringLiteral("domain")).toString();
            while (domain.startsWith(QLatin1Char('.'))) {
                domain.remove(0, 1);
            }
            // Una cookie rota no tumba el pedido: se descarta sola.
            if (drop || domain.isEmpty()) {
                ++request.droppedCookies;
                continue;
            }
            request.cookies.append(clean);
        }
    }
    *out = request;
    return true;
}

QJsonObject toJson(const Request &request)
{
    QJsonObject object{{QStringLiteral("v"), kProtocolVersion}, {QStringLiteral("type"), request.type}};
    if (request.type == QLatin1String("download")) {
        object.insert(QStringLiteral("url"), request.url);
        object.insert(QStringLiteral("title"), request.title);
        object.insert(QStringLiteral("browser"), request.browser);
        if (request.hasCookies) {
            object.insert(QStringLiteral("cookies"), request.cookies);
        }
        object.insert(QStringLiteral("source"), QStringLiteral("extension"));
    }
    return object;
}

QByteArray frame(const QJsonObject &object)
{
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray data(4, Qt::Uninitialized);
    qToLittleEndian<quint32>(quint32(json.size()), data.data());
    return data + json;
}

bool isHostInvocation(int argc, char *argv[])
{
    return argc >= 2 && argv[1] && std::strncmp(argv[1], "chrome-extension://", 19) == 0;
}

int run(int argc, char *argv[])
{
    // Solo nuestra extension: cualquier otro origen sale sin leer stdin.
    if (argc < 2 || QString::fromLocal8Bit(argv[1]) != allowedOrigin()) {
        return 2;
    }

    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("VideoDownloader"));
    app.setApplicationVersion(QStringLiteral(VIDEODOWNLOADER_VERSION));
    app.setOrganizationName(QStringLiteral("LGA"));
    app.setOrganizationDomain(QStringLiteral("lga.com"));

    QElapsedTimer clock;
    clock.start();

    QByteArray payload;
    const ReadStatus status = readMessage(&payload);
    if (status == ReadStatus::Timeout || status == ReadStatus::Closed) {
        return 3;
    }
    if (status == ReadStatus::TooLarge) {
        respondError(QStringLiteral("bad_request"), QStringLiteral("The message is too large."));
        return 1;
    }

    Request request;
    QString errorCode;
    QString errorMessage;
    if (!parseRequest(payload, false, &request, &errorCode, &errorMessage)) {
        respondError(errorCode, errorMessage);
        return 1;
    }

    QLocalSocket socket;
    if (request.type == QLatin1String("ping")) {
        bool running = connectToApp(socket, PING_CONNECT_MS);
        if (running) {
            QJsonObject reply;
            exchange(socket, toJson(request), clock, int(clock.elapsed()) + 1500, &reply);
            socket.disconnectFromServer();
        }
        QJsonObject response = baseResponse();
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("status"), QStringLiteral("pong"));
        response.insert(QStringLiteral("running"), running);
        respond(response);
        return 0;
    }

    // download: a la instancia abierta; si no hay, se lanza y se reintenta cada 200 ms.
    bool launched = false;
    bool connected = connectToApp(socket, 500);
    while (!connected && clock.elapsed() < CONNECT_DEADLINE_MS) {
        if (!launched) {
            if (!launchApp()) {
                respondError(QStringLiteral("app_start_failed"), QStringLiteral("Couldn't open LGA Video Downloader."));
                return 1;
            }
            launched = true;
        }
        QThread::msleep(RETRY_INTERVAL_MS);
        connected = connectToApp(socket, 300);
    }
    if (!connected) {
        respondError(QStringLiteral("app_timeout"), QStringLiteral("The app didn't answer."));
        return 1;
    }

    QJsonObject reply;
    if (!exchange(socket, toJson(request), clock, TOTAL_DEADLINE_MS, &reply)) {
        respondError(QStringLiteral("app_timeout"), QStringLiteral("The app didn't answer."));
        return 1;
    }
    socket.disconnectFromServer();

    QJsonObject response = baseResponse();
    if (reply.value(QStringLiteral("ok")).toBool()) {
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("status"), QStringLiteral("queued"));
        response.insert(QStringLiteral("launched"), launched);
    } else {
        response.insert(QStringLiteral("ok"), false);
        const QString code = reply.value(QStringLiteral("error")).toString();
        response.insert(QStringLiteral("error"), code == QLatin1String("bad_request") ? code : QStringLiteral("internal"));
        response.insert(QStringLiteral("message"), reply.value(QStringLiteral("message")).toString().left(300));
    }
    respond(response);
    return response.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}

} // namespace NativeHost

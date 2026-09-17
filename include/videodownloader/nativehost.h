#ifndef NATIVEHOST_H
#define NATIVEHOST_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

// Integracion con la extension de navegador (protocolo v1, Native Messaging).
//
// El navegador lanza el MISMO VideoDownloader.exe con argv[1] = "chrome-extension://<ID>/".
// En ese modo el proceso no abre ventana: lee UN mensaje de stdin, lo valida, lo reenvia a
// la instancia abierta de la app por QLocalSocket (lanzandola si hace falta), escribe UNA
// respuesta en stdout y sale.
namespace NativeHost {

// Nombre del host (el mismo que usa extension/lib/config.js y el JSON del host).
inline constexpr const char *kHostName = "com.lga.videodownloader";
inline constexpr int kProtocolVersion = 1;

// Limites del contrato v1 (los mismos que extension/lib/config.js).
inline constexpr qint64 kMaxMessageBytes = 8 * 1024 * 1024;
inline constexpr int kMaxUrlChars = 4096;
inline constexpr int kMaxTitleChars = 512;
inline constexpr int kMaxBrowserChars = 32;
inline constexpr int kMaxCookies = 3000;

// ID fijo de la extension (sale de extension/EXTENSION_ID.txt via CMake).
QString extensionId();
// "chrome-extension://<ID>/": unico origen aceptado.
QString allowedOrigin();

// Pedido ya validado. `cookies` solo trae las cookies aceptadas (las que tenian tabs o
// saltos de linea, o dominio vacio, se descartan una por una).
struct Request {
    QString type;       // ping | download | activate (activate: solo por el socket interno)
    QString url;
    QString title;
    QString browser;
    QJsonArray cookies;
    bool hasCookies = false;  // el mensaje traia el campo cookies (aunque quede vacio)
    int droppedCookies = 0;
};

// Valida un mensaje JSON del contrato v1. Devuelve false con `errorCode` en
// bad_request | unsupported_version y un mensaje corto en ingles (nunca con datos del pedido).
// `allowActivate`: el socket interno acepta ademas {"v":1,"type":"activate"}.
bool parseRequest(const QByteArray &json, bool allowActivate, Request *out, QString *errorCode, QString *message);

// Mensaje validado listo para reenviar por el socket.
QJsonObject toJson(const Request &request);

// Framing del socket interno: UInt32 little-endian + JSON UTF-8.
QByteArray frame(const QJsonObject &object);

// Si el proceso fue lanzado por el navegador (argv[1] empieza con chrome-extension://).
// Se decide ANTES de construir QApplication.
bool isHostInvocation(int argc, char *argv[]);

// Corre el modo host completo y devuelve el codigo de salida del proceso.
int run(int argc, char *argv[]);

} // namespace NativeHost

#endif // NATIVEHOST_H

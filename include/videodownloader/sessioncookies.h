#ifndef SESSIONCOOKIES_H
#define SESSIONCOOKIES_H

#include <QByteArray>
#include <QJsonArray>
#include <QString>

// Cookies de sesion que manda la extension de navegador para UN item de la cola.
//
// Viven en memoria (DownloadOptions::sessionCookies) mientras el item esta en la lista, y en
// disco solo mientras corre yt-dlp: un archivo temporal por descarga en
// <AppLocalData>/session-cookies/, que se borra cuando el proceso termina (ok, error, cancel o
// cierre) y se barre al arrancar por si la app murio a mitad. Nunca van a config, log ni
// argumentos (solo la ruta del archivo).
namespace SessionCookies {

// Arma un cookies.txt Netscape. Cada cookie aceptada es una linea:
//   [#HttpOnly_]dominio  includeSubdomains  path  secure  expiracion  nombre  valor
// hostOnly=false -> dominio con punto inicial y TRUE; hostOnly=true -> sin punto y FALSE.
// Expiracion entera (floor) o 0 si es de sesion. Las cookies invalidas se saltean.
QByteArray toNetscape(const QJsonArray &cookies, int *accepted = nullptr);

// <AppLocalData>/session-cookies
QString directory();

// Escribe el contenido en un archivo nuevo y lo CIERRA. Devuelve la ruta o vacio si falla.
QString writeTempFile(const QByteArray &content);

// Borra el archivo, con reintentos cortos (yt-dlp recien muerto puede tenerlo abierto).
bool removeFile(const QString &path);

// Borra todo lo que haya quedado en la carpeta (restos de un cierre abrupto).
int sweep();

} // namespace SessionCookies

#endif // SESSIONCOOKIES_H

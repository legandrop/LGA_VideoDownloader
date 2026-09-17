#ifndef HOSTREGISTRATION_H
#define HOSTREGISTRATION_H

#include <QByteArray>
#include <QString>

// Donde vive la extension que el usuario carga con "Load unpacked" y como se registra el
// host de Native Messaging para que Chrome, Brave y Edge encuentren al exe.
//
// Windows: una sola clave HKCU\Software\Google\Chrome\NativeMessagingHosts\<host> (la leen
// Chrome, Brave y Edge) apuntando a un JSON junto al exe con "path" relativo.
// macOS (no verificado en Mac): JSON con path absoluto en la carpeta NativeMessagingHosts de
// cada navegador instalado, y la extension copiada a Application Support.
namespace HostRegistration {

// Carpeta de la extension para mostrar en Help y abrir desde ahi.
QString extensionFolder();

// "version" del manifest.json de esa carpeta (vacio si no se puede leer).
QString installedExtensionVersion();

// Contenido del JSON del host para un "path" dado.
QByteArray hostManifest(const QString &executablePath);

// Crea o repara el registro del host al arrancar la instancia principal. Desde el arbol de
// desarrollo solo escribe lo que FALTA (no pisa lo que dejo una copia instalada). Devuelve una
// linea corta para el log (vacia si no hubo nada que cambiar).
QString ensureRegistered();

} // namespace HostRegistration

#endif // HOSTREGISTRATION_H

#ifndef BROWSERDETECT_H
#define BROWSERDETECT_H

#include <QList>
#include <QString>

// Navegadores instalados de los que yt-dlp puede tomar la sesion (--cookies-from-browser).
// Se detectan por la carpeta de perfil, que es justo lo que yt-dlp necesita leer: un
// navegador instalado pero nunca abierto no tiene cookies y no sirve.
namespace BrowserDetect {

struct Browser {
    QString key;          // valor para --cookies-from-browser: "firefox", "chrome", ...
    QString name;         // nombre visible: "Firefox"
    bool supported;       // false: en esta plataforma yt-dlp no puede leer sus cookies
    bool recommended;
};

// Lista en el orden en que se ofrece en la UI.
QList<Browser> detectInstalled();

// Nombre visible para una clave ("firefox" -> "Firefox"); vacio -> "the browser".
QString displayName(const QString &key);

// En Windows, Chrome/Edge/Brave/Opera/Vivaldi cifran las cookies con "app-bound
// encryption" y yt-dlp no las puede descifrar.
bool isReadableOnThisPlatform(const QString &key);

} // namespace BrowserDetect

#endif // BROWSERDETECT_H

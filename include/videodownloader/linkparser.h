#ifndef LINKPARSER_H
#define LINKPARSER_H

#include <QString>
#include <QStringList>

// Lectura del texto pegado en la caja de links. Se acepta cualquier sitio: si yt-dlp no lo
// soporta, la tarjeta termina con "This site isn't supported".
namespace LinkParser {

struct Result {
    QStringList links;        // links http(s), sin repetidos, en el orden pegado
    int ignoredWords = 0;     // pedazos de texto que no son links
    QString ignoredText;      // ese texto, para mostrarlo resumido
};

// Separa por espacios, saltos de linea, comas y punto y coma, y se queda con lo que parece
// un link: http(s)://..., o un dominio conocido / "www." sin esquema (se le agrega https://).
Result parse(const QString &text);

// Nombre visible de los sitios conocidos ("YouTube", "SoundCloud"...); vacio para el resto
// (la tarjeta usa entonces el extractor que informe yt-dlp).
QString siteName(const QString &url);

} // namespace LinkParser

#endif // LINKPARSER_H

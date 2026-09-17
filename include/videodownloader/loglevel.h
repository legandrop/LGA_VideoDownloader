#ifndef LOGLEVEL_H
#define LOGLEVEL_H

#include <QString>

// Nivel de una linea del log visible. Detail = continuacion de la linea anterior.
enum class LogLevel {
    Info,
    Done,
    Warning,
    Error,
    Detail
};

// Deduce el nivel de una linea que no lo trae explicito (salida de yt-dlp, mensajes de las
// tools). Solo mira el comienzo: es barato y alcanza para los prefijos que usa yt-dlp.
inline LogLevel classifyLogLine(const QString &line)
{
    if (line.startsWith(QLatin1String("ERROR"), Qt::CaseInsensitive)
        || line.startsWith(QStringLiteral("✗"))) {
        return LogLevel::Error;
    }
    if (line.startsWith(QLatin1String("WARNING"), Qt::CaseInsensitive)) {
        return LogLevel::Warning;
    }
    if (line.startsWith(QStringLiteral("✓"))) {
        return LogLevel::Done;
    }
    return LogLevel::Info;
}

#endif // LOGLEVEL_H

#include "videodownloader/versioncompare.h"

#include <QRegularExpression>
#include <QVersionNumber>

namespace VersionCompare {

namespace {

// Saca el prefijo "v" y los espacios. "v0.2" y " 0.2 " son la misma version.
QString stripped(const QString &rawVersion)
{
    QString candidate = rawVersion.trimmed();
    if (candidate.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        candidate = candidate.mid(1).trimmed();
    }
    return candidate;
}

bool parseMajorMinor(const QString &rawVersion, int *majorOut, QString *minorDigitsOut)
{
    const QString candidate = stripped(rawVersion);
    if (candidate.isEmpty()) {
        return false;
    }

    static const QRegularExpression kVersionRegex(QStringLiteral("^([0-9]+)\\.([0-9]+)$"));
    const QRegularExpressionMatch match = kVersionRegex.match(candidate);
    if (!match.hasMatch()) {
        return false;
    }

    bool majorOk = false;
    const int major = match.captured(1).toInt(&majorOk);
    const QString minorDigits = match.captured(2);
    if (!majorOk || minorDigits.isEmpty()) {
        return false;
    }

    if (majorOut) {
        *majorOut = major;
    }
    if (minorDigitsOut) {
        *minorDigitsOut = minorDigits;
    }
    return true;
}

// Normaliza "X.Y" a "X.<minor con ancho fijo>". false si no tiene esa forma.
QVersionNumber toVersionNumber(const QString &rawVersion, bool *okOut)
{
    if (okOut) {
        *okOut = false;
    }

    int major = 0;
    QString minorDigits;
    if (!parseMajorMinor(rawVersion, &major, &minorDigits)) {
        return {};
    }

    if (minorDigits.size() < kMinorWidth) {
        // Zero-padding A LA DERECHA: es lo que hace que 1.5 > 1.05 > 1.005, porque el
        // minor se escribe con ancho fijo y un tag abreviado (0.2) vale por 0.200.
        minorDigits += QString(kMinorWidth - minorDigits.size(), QLatin1Char('0'));
    }

    const QString normalized = QStringLiteral("%1.%2").arg(major).arg(minorDigits);
    qsizetype parsePosition = -1;
    const QVersionNumber version = QVersionNumber::fromString(normalized, &parsePosition);
    const bool ok = !version.isNull() && parsePosition == normalized.size();
    if (okOut) {
        *okOut = ok;
    }
    return ok ? version : QVersionNumber();
}

} // namespace

bool isNewer(const QString &candidateVersion, const QString &referenceVersion)
{
    bool candidateOk = false;
    bool referenceOk = false;
    const QVersionNumber candidate = toVersionNumber(candidateVersion, &candidateOk);
    const QVersionNumber reference = toVersionNumber(referenceVersion, &referenceOk);

    if (candidateOk && referenceOk) {
        return QVersionNumber::compare(candidate, reference) > 0;
    }

    // Fallback para esquemas que no son "X.Y" (p.ej. un semver "1.2.3"): mejor
    // comparar crudo que negar el update. Exige que NINGUNO de los dos haya
    // parseado como "X.Y", porque mezclar escalas (paddeado vs. crudo) da
    // resultados sin sentido.
    if (candidateOk != referenceOk) {
        return false;
    }

    const QVersionNumber rawCandidate = QVersionNumber::fromString(stripped(candidateVersion));
    const QVersionNumber rawReference = QVersionNumber::fromString(stripped(referenceVersion));
    if (rawCandidate.isNull() || rawReference.isNull()) {
        return false;
    }
    return QVersionNumber::compare(rawCandidate, rawReference) > 0;
}

} // namespace VersionCompare

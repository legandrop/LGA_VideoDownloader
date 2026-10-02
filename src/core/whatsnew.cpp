#include "videodownloader/whatsnew.h"
#include "videodownloader/theme.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QVariant>

#include <algorithm>

namespace WhatsNew {

namespace {

constexpr int kMinorWidth = 3;

// Segmentos de la version con la regla LGA (ver compareVersions), sin ceros finales. Vacio si
// no es numerica o desborda.
QList<qint64> canonical(const QString &raw)
{
    QString text = raw.trimmed();
    if (text.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        text = text.mid(1);
    }
    static const QRegularExpression re(QStringLiteral("^[0-9]{1,15}(\\.[0-9]{1,15})*$"));
    if (!re.match(text).hasMatch()) {
        return {};
    }
    const QStringList pieces = text.split(QLatin1Char('.'));
    QString minor = pieces.size() > 1 ? pieces.at(1) : QStringLiteral("0");
    // El minor se escribe con ancho fijo: un tag abreviado (0.96) vale por 0.960.
    if (minor.size() < kMinorWidth) {
        minor += QString(kMinorWidth - minor.size(), QLatin1Char('0'));
    }
    QList<qint64> segments{pieces.at(0).toLongLong(), minor.toLongLong()};
    for (qsizetype i = 2; i < pieces.size(); ++i) {
        segments.append(pieces.at(i).toLongLong());
    }
    while (segments.size() > 1 && segments.last() == 0) {
        segments.removeLast();
    }
    return segments;
}

bool parseKind(const QString &text, Kind *out)
{
    if (text == QLatin1String("new")) {
        *out = Kind::New;
    } else if (text == QLatin1String("improved")) {
        *out = Kind::Improved;
    } else if (text == QLatin1String("fixed")) {
        *out = Kind::Fixed;
    } else {
        return false;
    }
    return true;
}

// Lista de textos opcional: ausente = vacia; presente y mal formada = error.
bool readStringList(const QJsonObject &object, const QString &key, QStringList *out)
{
    const QJsonValue value = object.value(key);
    out->clear();
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    if (!value.isArray()) {
        return false;
    }
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isString()) {
            return false;
        }
        out->append(entry.toString().trimmed());
    }
    return true;
}

Notes invalid(const QString &reason)
{
    Notes notes;
    notes.error = reason;
    return notes;
}

} // namespace

Notes parse(const QByteArray &json, const QString &expectedProduct)
{
    if (json.size() > kMaxBytes) {
        return invalid(QStringLiteral("supera %1 bytes").arg(kMaxBytes));
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return invalid(QStringLiteral("JSON invalido: %1").arg(parseError.errorString()));
    }
    const QJsonObject root = doc.object();
    const QJsonValue schema = root.value(QStringLiteral("schemaVersion"));
    if (!schema.isDouble() || schema.toInt(-1) != kSchemaVersion) {
        return invalid(QStringLiteral("schemaVersion no soportado: %1").arg(schema.toVariant().toString()));
    }
    const QString product = root.value(QStringLiteral("product")).toString();
    if (!expectedProduct.isEmpty() && product != expectedProduct) {
        return invalid(QStringLiteral("product '%1' no es '%2'").arg(product, expectedProduct));
    }
    const QJsonValue versionsValue = root.value(QStringLiteral("versions"));
    if (!versionsValue.isArray()) {
        return invalid(QStringLiteral("falta el array versions"));
    }

    Notes notes;
    const QJsonArray versions = versionsValue.toArray();
    for (qsizetype i = 0; i < versions.size(); ++i) {
        const QJsonObject versionObject = versions.at(i).toObject();
        VersionNotes entry;
        entry.version = versionObject.value(QStringLiteral("version")).toString().trimmed();
        if (!versions.at(i).isObject() || !isValidVersion(entry.version)) {
            return invalid(QStringLiteral("versions[%1]: version invalida").arg(i));
        }
        const QJsonValue dateValue = versionObject.value(QStringLiteral("date"));
        if (!dateValue.isUndefined() && !dateValue.isNull() && !dateValue.isString()) {
            return invalid(QStringLiteral("versions[%1].date no es texto").arg(i));
        }
        entry.date = dateValue.toString().trimmed();
        const QJsonValue itemsValue = versionObject.value(QStringLiteral("items"));
        if (!itemsValue.isArray()) {
            return invalid(QStringLiteral("versions[%1].items no es un array").arg(i));
        }
        const QJsonArray items = itemsValue.toArray();
        for (qsizetype j = 0; j < items.size(); ++j) {
            const QJsonObject itemObject = items.at(j).toObject();
            Item item;
            const QJsonValue textValue = itemObject.value(QStringLiteral("text"));
            item.text = textValue.toString().trimmed();
            QStringList unused;
            if (!items.at(j).isObject() || !parseKind(itemObject.value(QStringLiteral("kind")).toString(), &item.kind)
                || !textValue.isString() || item.text.isEmpty()
                || !readStringList(itemObject, QStringLiteral("platform"), &item.platforms)
                || !readStringList(itemObject, QStringLiteral("for"), &unused)
                || !readStringList(itemObject, QStringLiteral("edition"), &unused)) {
                return invalid(QStringLiteral("versions[%1].items[%2] invalido").arg(i).arg(j));
            }
            entry.items.append(item);
        }
        notes.versions.append(entry);
    }
    // No se confia en el orden del archivo.
    std::stable_sort(notes.versions.begin(), notes.versions.end(), [](const VersionNotes &a, const VersionNotes &b) {
        return compareVersions(a.version, b.version) > 0;
    });
    notes.valid = true;
    return notes;
}

int compareVersions(const QString &left, const QString &right, bool *okOut)
{
    const QList<qint64> a = canonical(left);
    const QList<qint64> b = canonical(right);
    const bool ok = !a.isEmpty() && !b.isEmpty();
    if (okOut) {
        *okOut = ok;
    }
    if (!ok) {
        return 0;
    }
    for (qsizetype i = 0; i < qMax(a.size(), b.size()); ++i) {
        const qint64 x = a.value(i, 0);
        const qint64 y = b.value(i, 0);
        if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    return 0;
}

bool isValidVersion(const QString &version)
{
    return !canonical(version).isEmpty();
}

QString currentPlatform()
{
#ifdef Q_OS_MAC
    return QStringLiteral("mac");
#else
    return QStringLiteral("win");
#endif
}

QList<VersionNotes> range(const Notes &notes, const QString &installedExclusive,
                          const QString &offeredInclusive, const QString &platform)
{
    QList<VersionNotes> out;
    const bool hasFloor = !installedExclusive.trimmed().isEmpty();
    const bool hasCeiling = !offeredInclusive.trimmed().isEmpty();
    // Un extremo que no es una version no sirve para cortar: mejor nada que notas que no tocan.
    if (!notes.valid || (hasFloor && !isValidVersion(installedExclusive))
        || (hasCeiling && !isValidVersion(offeredInclusive))) {
        return out;
    }
    for (const VersionNotes &entry : notes.versions) {
        if ((hasFloor && compareVersions(entry.version, installedExclusive) <= 0)
            || (hasCeiling && compareVersions(entry.version, offeredInclusive) > 0)) {
            continue;
        }
        VersionNotes kept = entry;
        kept.items.clear();
        for (const Item &item : entry.items) {
            if (item.platforms.isEmpty() || item.platforms.contains(platform, Qt::CaseInsensitive)) {
                kept.items.append(item);
            }
        }
        if (!kept.items.isEmpty()) {
            out.append(kept);
        }
    }
    return out;
}

QString toHtml(const QList<VersionNotes> &versions)
{
    // Rotulos en los colores de la familia LGA: NEW verde, IMPROVED azul, FIXED ambar.
    const auto kindStyle = [](Kind kind) -> QPair<QString, QString> {
        switch (kind) {
        case Kind::New: return {QStringLiteral("NEW"), QLatin1String(Theme::kWhatsNewNew)};
        case Kind::Improved: return {QStringLiteral("IMPROVED"), QLatin1String(Theme::kWhatsNewImproved)};
        case Kind::Fixed: return {QStringLiteral("FIXED"), QLatin1String(Theme::kWhatsNewFixed)};
        }
        return {};
    };
    QString html = QStringLiteral("<div style='color:%1;'>").arg(QLatin1String(Theme::kHelpBody));
    bool first = true;
    for (const VersionNotes &entry : versions) {
        QString heading = QStringLiteral("<span style='color:%1; font-weight:600;'>v%2</span>")
                              .arg(QLatin1String(Theme::kTextEmphasis), entry.version.toHtmlEscaped());
        if (!entry.date.isEmpty()) {
            heading += QStringLiteral("<span style='color:%1;'>&nbsp;&nbsp;%2</span>")
                           .arg(QLatin1String(Theme::kTextFaint), entry.date.toHtmlEscaped());
        }
        html += QStringLiteral("<p style='margin-top:%1px; margin-bottom:2px;'>%2</p>")
                    .arg(first ? 0 : 14)
                    .arg(heading);
        first = false;
        for (const Kind kind : {Kind::New, Kind::Improved, Kind::Fixed}) {
            QString rows;
            for (const Item &item : entry.items) {
                if (item.kind == kind) {
                    // Vinetas en tabla y no en <ul>: el <ul> de QTextDocument sangra 40 px.
                    rows += QStringLiteral("<tr><td width='14' valign='top'>&#8226;</td><td valign='top'>%1</td></tr>")
                                .arg(item.text.toHtmlEscaped());
                }
            }
            if (rows.isEmpty()) {
                continue;
            }
            const auto style = kindStyle(kind);
            html += QStringLiteral("<p style='margin-top:6px; margin-bottom:1px; color:%1; font-size:11px; "
                                   "font-weight:600;'>%2</p><table cellspacing='0' cellpadding='1' width='100%'>%3</table>")
                        .arg(style.second, style.first, rows);
        }
    }
    return html + QStringLiteral("</div>");
}

AfterInstall afterInstallAction(const QString &lastSeen, const QString &current)
{
    if (!isValidVersion(current)) {
        return AfterInstall::Nothing;
    }
    if (lastSeen.trimmed().isEmpty() || !isValidVersion(lastSeen)) {
        return AfterInstall::SaveOnly;
    }
    return compareVersions(lastSeen, current) < 0 ? AfterInstall::Show : AfterInstall::Nothing;
}

} // namespace WhatsNew

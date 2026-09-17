#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QFont>
#include <QString>

class QApplication;

// Tokens visuales de la app (tema oscuro LGA) y la hoja de estilo global. Todo color o
// medida de la UI sale de aca; los widgets solo eligen objectName y propiedades.
namespace Theme {

// Fondos
inline constexpr const char *kWindow = "#161616";
inline constexpr const char *kTabBar = "#101010";
inline constexpr const char *kCard = "#1d1d1d";
inline constexpr const char *kTile = "#242424";
inline constexpr const char *kField = "#1a1a1a";
inline constexpr const char *kLogBody = "#171717";
inline constexpr const char *kDialog = "#1E1E1E";

// Bordes
inline constexpr const char *kBorder = "#303030";
inline constexpr const char *kFieldBorder = "#2f2f2f";
inline constexpr const char *kDivider = "#262626";

// Texto
inline constexpr const char *kText = "#B2B2B2";
inline constexpr const char *kTextStrong = "#CCCCCC";
inline constexpr const char *kTextTitle = "#D6D6D6";
inline constexpr const char *kTextBright = "#E6E6E6";
inline constexpr const char *kTextMuted = "#8f8f8f";
inline constexpr const char *kTextCaption = "#7b7b7b";
inline constexpr const char *kTextFaint = "#6f6f6f";
inline constexpr const char *kTextPlaceholder = "#555555";
inline constexpr const char *kIcon = "#6a6a6a";
inline constexpr const char *kAccent = "#774dcb";
inline constexpr const char *kLink = "#9D8FE0";

// Estados
inline constexpr const char *kOk = "#a8d86a";
inline constexpr const char *kWarn = "#d4a437";
inline constexpr const char *kError = "#e8836f";
inline constexpr const char *kRun = "#b3a6ec";

// Progreso
inline constexpr const char *kProgressTrack = "#393959";
inline constexpr const char *kProgressBorder = "#444444";
inline constexpr const char *kProgressFill = "#6a55c9";
inline constexpr const char *kProgressDone = "#4c6b33";

inline QColor color(const char *hex) { return QColor(QLatin1String(hex)); }

// Fuente de interfaz (Inter) en pixeles, con peso opcional.
QFont uiFont(qreal pixelSize, int weight = QFont::Normal);
// Fuente monoespaciada del log.
QFont monoFont(qreal pixelSize);

// Carga Inter embebida, estilo Fusion, paleta oscura y hoja de estilo global.
void apply(QApplication &app);

QString styleSheet();

} // namespace Theme

#endif // THEME_H

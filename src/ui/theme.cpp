#include "videodownloader/theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QScreen>
#include <QStyleFactory>

namespace {

// QFont y QSS solo aceptan pixeles enteros. Los tamanos del diseno (13.5px, 12.5px) se
// expresan en puntos segun el DPI logico: 96 en Windows (1px = 0.75pt) y 72 en macOS.
qreal pointsPerPixel()
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    const qreal dpi = screen ? screen->logicalDotsPerInchY() : 96.0;
    return 72.0 / (dpi > 0 ? dpi : 96.0);
}

QString fs(qreal px)
{
    return QString::number(px * pointsPerPixel(), 'f', 3) + QStringLiteral("pt");
}

} // namespace

namespace Theme {

QFont uiFont(qreal pixelSize, int weight)
{
    QFont font(QStringLiteral("Inter"));
    font.setPointSizeF(pixelSize * pointsPerPixel());
    font.setWeight(static_cast<QFont::Weight>(weight));
    return font;
}

QFont monoFont(qreal pixelSize)
{
    QFont font;
    font.setFamilies({QStringLiteral("JetBrains Mono"), QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
                      QStringLiteral("Menlo"), QStringLiteral("Monaco"), QStringLiteral("DejaVu Sans Mono")});
    font.setStyleHint(QFont::Monospace);
    font.setPointSizeF(pixelSize * pointsPerPixel());
    return font;
}

void apply(QApplication &app)
{
    // Inter embebida: el diseno se mide con ella y no todas las maquinas la tienen instalada.
    for (const char *file : {":/fonts/Inter-Regular.ttf", ":/fonts/Inter-Medium.ttf", ":/fonts/Inter-SemiBold.ttf"}) {
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(file)) < 0) {
            qWarning("No se pudo cargar la fuente embebida %s", file);
        }
    }

    // Fusion en las dos plataformas: la hoja de estilo se ve igual en Windows y macOS.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, color(kWindow));
    palette.setColor(QPalette::WindowText, color(kText));
    palette.setColor(QPalette::Base, color(kField));
    palette.setColor(QPalette::AlternateBase, color(kCard));
    palette.setColor(QPalette::Text, color(kText));
    palette.setColor(QPalette::PlaceholderText, color(kTextPlaceholder));
    palette.setColor(QPalette::Button, QColor(0x2a, 0x2a, 0x2a));
    palette.setColor(QPalette::ButtonText, color(kText));
    palette.setColor(QPalette::Highlight, QColor(0x39, 0x34, 0x55));
    palette.setColor(QPalette::HighlightedText, color(kTextBright));
    palette.setColor(QPalette::ToolTipBase, color(kTile));
    palette.setColor(QPalette::ToolTipText, color(kText));
    palette.setColor(QPalette::Link, color(kLink));
    palette.setColor(QPalette::Disabled, QPalette::Text, color(kTextPlaceholder));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, color(kTextPlaceholder));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, color(kTextPlaceholder));
    app.setPalette(palette);

    app.setFont(uiFont(14));
    app.setStyleSheet(styleSheet());
}

QString styleSheet()
{
    // Reglas por objectName/propiedad, nunca "QWidget { background }" global: esa regla
    // pinta cada label y contenedor con el fondo de la ventana y deja huecos de otro color
    // adentro de las tarjetas.
    QString qss = QStringLiteral(R"QSS(
QMainWindow, QWidget#centralWidget, QWidget#content { background-color: @window; }
QLabel { background: transparent; color: @text; }
QToolTip { background-color: @tile; color: @text; border: 1px solid #333333; padding: 4px 6px; }

QFrame#card { background-color: @card; border: none; border-radius: 8px; }
QLabel#cardTitle { color: @textStrong; font-size: @fs14; font-weight: 600; }
QLabel#cardCaption { color: @textFaint; font-size: @fs13; }
QLabel#fieldLabel { color: @textMuted; font-size: @fs12_5; }
QLabel#note { color: @textFaint; font-size: @fs12; }

/* Botones */
QPushButton {
    background-color: #2a2a2a; color: @text; border: none; border-radius: 5px;
    padding: 0px 12px; min-height: 30px; max-height: 30px; font-size: @fs13; font-weight: 500;
}
QPushButton:hover { background-color: #383838; }
QPushButton:pressed { background-color: #242424; }
QPushButton:disabled { background-color: #232323; color: #5a5a5a; }
QPushButton[variant="primary"] { background-color: #443a91; color: #DDDBEE; font-weight: 600; }
QPushButton[variant="primary"]:hover { background-color: #5243a8; }
QPushButton[variant="primary"]:pressed { background-color: #3b3280; }
QPushButton[variant="primary"]:disabled { background-color: #2c2848; color: #7d7a95; }
QPushButton[variant="ghost"] { background-color: transparent; color: @textMuted; padding: 0px 8px; }
QPushButton[variant="ghost"]:hover { background-color: #2a2a2a; color: @textStrong; }
QPushButton[variant="ghost"]:disabled { background-color: transparent; color: #4a4a4a; }
QPushButton[btnSize="sm"] { min-height: 26px; max-height: 26px; font-size: @fs12_5; padding: 0px 10px; }
QPushButton[variant="ghost"][btnSize="sm"] { padding: 0px 8px; }
QPushButton[btnSize="icon"] { min-height: 26px; max-height: 26px; min-width: 28px; max-width: 28px; padding: 0px; }
QPushButton[btnSize="big"] { min-height: 58px; max-height: 58px; font-size: @fs13; }

/* Campos */
QPlainTextEdit#urlInput {
    background-color: @field; color: @textCaption; border: 1px solid @fieldBorder; border-radius: 3px;
    padding: 4px 6px; font-size: @fs13_5; selection-background-color: #393455;
}
QPlainTextEdit#urlInput:focus { border-color: #555555; }
QComboBox {
    background-color: @field; color: @textCaption; border: 1px solid @fieldBorder; border-radius: 3px;
    padding: 0px 0px 0px 8px; min-height: 28px; max-height: 28px; font-size: @fs13_5;
}
QComboBox:focus, QComboBox:on { border-color: #555555; }
QComboBox#cookiesCombo { color: #9a9a9a; }
QComboBox#cookiesCombo[attention="true"] { color: @error; border-color: #5c3330; }
QComboBox:disabled { color: #555555; }
QComboBox::drop-down { subcontrol-origin: border; subcontrol-position: top right; width: 22px; border: none; border-left: 1px solid @fieldBorder; }
QComboBox::down-arrow { image: none; width: 0px; height: 0px; border: none; }
QComboBox QAbstractItemView {
    background-color: @field; color: @text; border: 1px solid @fieldBorder; outline: 0px;
    selection-background-color: #393455; selection-color: #DDDBEE; padding: 2px;
}
QComboBox QAbstractItemView::item { min-height: 26px; padding: 0px 8px; }
QComboBox QAbstractItemView::item:disabled { color: #5a5a5a; }
QFrame#field { background-color: @field; border: 1px solid @fieldBorder; border-radius: 3px; min-height: 28px; max-height: 28px; }
QLabel#fieldValue, ElidedLabel#fieldValue { color: @textCaption; font-size: @fs13_5; }

/* Barra superior */
QToolButton#cornerButton { background: transparent; border: none; padding: 0px; }
QPushButton#noticeChip {
    min-height: 22px; max-height: 22px; padding: 0px 9px; border-radius: 4px; font-size: @fs12;
    font-weight: 600; background-color: #26223f; border: 1px solid #3B316A; color: @run;
}
QPushButton#noticeChip:hover { background-color: #2f2950; }
QPushButton#noticeChip[tone="neutral"] { background-color: #2b2b2b; border-color: #383838; color: #c5c8c7; }
QPushButton#noticeChip[tone="err"] { background-color: #35211f; border-color: #5c3330; color: @error; }
QPushButton#noticeChip[tone="err"]:hover { background-color: #43291f; }

/* Chips */
QFrame#chip { background-color: #2b2b2b; border: 1px solid #383838; border-radius: 4px; min-height: 18px; max-height: 18px; }
QFrame#chip QLabel { color: #c5c8c7; font-size: @fs11_5; font-weight: 600; }
QFrame#chip[tone="ok"] { background-color: #1f2a17; border-color: #3a4d27; }
QFrame#chip[tone="ok"] QLabel { color: @ok; }
QFrame#chip[tone="err"] { background-color: #35211f; border-color: #5c3330; }
QFrame#chip[tone="err"] QLabel { color: @error; }
QFrame#chip[tone="warn"] { background-color: #332b16; border-color: #5a4820; }
QFrame#chip[tone="warn"] QLabel { color: @warn; }
QFrame#chip[tone="run"] { background-color: #26223f; border-color: #3B316A; }
QFrame#chip[tone="run"] QLabel { color: @run; }
QFrame#chip[tone="src"] { background-color: transparent; border-color: #333333; }
QFrame#chip[tone="src"] QLabel { color: @textMuted; font-weight: 500; }

/* Cola */
QScrollArea#queueScroll { background: transparent; border: none; }
QWidget#queueViewport, QWidget#queueList { background: transparent; }
QFrame#queueFooter { background: transparent; border: none; border-top: 1px solid @divider; }
QLabel#emptyTitle { color: @text; font-size: @fs15; font-weight: 500; }
QLabel#emptyText { color: @textFaint; font-size: @fs13; }
QFrame#tile { background-color: @tile; border: 1px solid @tile; border-radius: 8px; }
QFrame#tile[state="run"] { background-color: #242131; border-color: #3B316A; }
QFrame#tile[state="err"] { background-color: #271e1d; border-color: #5c3330; }
ElidedLabel#itemTitle { color: @textTitle; font-size: @fs13_5; font-weight: 500; }
QLabel#meta { color: @textFaint; font-size: @fs12; }
QLabel#percent { color: @text; font-size: @fs12; }
QLabel#errorHeadline { color: @error; font-size: @fs13_5; font-weight: 600; }
QFrame#fixBox { background-color: #2a201e; border: 1px solid #4a2d2a; border-radius: 6px; }
QLabel#fixText { color: #a9a9ae; font-size: @fs12_5; }
QLabel#footerLabel { color: @textMuted; font-size: @fs12_5; }
QLabel#footerValue { color: @text; font-size: @fs12_5; }
QLabel#footerAlert { color: @error; font-size: @fs12_5; }

/* Log */
LogCanvas { background-color: #171717; border: none; border-bottom-left-radius: 8px; border-bottom-right-radius: 8px; }
QFrame#logHeader { background: transparent; border: none; border-bottom: 1px solid @divider; }

/* Scrollbars */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px 2px 2px 0px; }
QScrollBar::handle:vertical { background-color: #3a3a3a; min-height: 30px; border-radius: 3px; }
QScrollBar::handle:vertical:hover { background-color: #4a4a4a; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: none; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
QScrollBar:horizontal { height: 0px; }

/* Ayuda */
QLabel#helpTitle { color: #E0E0E0; font-size: @fs18; font-weight: 600; }
QLabel#helpVersion { color: @textMuted; font-size: @fs14; }
QFrame#updateBox { background-color: @tile; border: 1px solid @tile; border-radius: 7px; }
QFrame#updateBox[accent="true"] { border-color: #3B316A; }
QLabel#updateTitle { color: @textBright; font-size: @fs13_5; }
QLabel#caption { color: @textCaption; font-size: @fs12; }
QLabel#kvKey { color: @textMuted; font-size: @fs13; }
QLabel#kvKeyStrong { color: @textBright; font-size: @fs13; font-weight: 500; }
QLabel#kvValue { color: @text; font-size: @fs13; }
QLabel#helpBody { color: #a9a9ae; font-size: @fs13; }
QFrame#helpRule { background-color: @border; border: none; min-height: 1px; max-height: 1px; }
QProgressBar#updateProgress { background-color: @progressTrack; border: 1px solid @progressBorder; border-radius: 4px; min-height: 8px; max-height: 8px; }
QProgressBar#updateProgress::chunk { background-color: @progressFill; border-radius: 3px; }
QPushButton#closeButton { border: 1px solid #3B316A; }
)QSS");

    const QList<QPair<const char *, QString>> tokens = {
        {"@window", kWindow}, {"@card", kCard}, {"@tile", kTile}, {"@fieldBorder", kFieldBorder},
        {"@field", kField}, {"@border", kBorder}, {"@divider", kDivider},
        {"@textStrong", kTextStrong}, {"@textTitle", kTextTitle}, {"@textBright", kTextBright},
        {"@textMuted", kTextMuted}, {"@textCaption", kTextCaption}, {"@textFaint", kTextFaint},
        {"@text", kText}, {"@ok", kOk}, {"@warn", kWarn}, {"@error", kError}, {"@run", kRun},
        {"@progressTrack", kProgressTrack}, {"@progressBorder", kProgressBorder}, {"@progressFill", kProgressFill},
        {"@fs13_5", fs(13.5)}, {"@fs12_5", fs(12.5)}, {"@fs11_5", fs(11.5)},
        {"@fs18", fs(18)}, {"@fs15", fs(15)}, {"@fs14", fs(14)}, {"@fs13", fs(13)}, {"@fs12", fs(12)},
    };
    // Orden: los nombres largos primero ("@textStrong" antes que "@text", "@fs13_5" antes que "@fs13").
    for (const auto &token : tokens) {
        qss.replace(QLatin1String(token.first), token.second);
    }
    return qss;
}

} // namespace Theme

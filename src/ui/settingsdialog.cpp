#include "videodownloader/settingsdialog.h"
#include "videodownloader/theme.h"
#include "videodownloader/uiwidgets.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {

// Version de una tool tal como entra en su fila. Con un build nocturno de ffmpeg
// ("N-117208-gbd22d7e601-20240927", 203 px) la fila quedaba cortada: los nocturnos se muestran por
// su fecha y cualquier otra cadena larga se recorta; la completa va en el tooltip.
QString shortToolVersion(const QString &version)
{
    static const QRegularExpression nightly(QStringLiteral("^N-\\d+-g[0-9a-f]+-(\\d{4})(\\d{2})(\\d{2})$"));
    const QRegularExpressionMatch match = nightly.match(version);
    if (match.hasMatch()) {
        return QStringLiteral("%1-%2-%3").arg(match.captured(1), match.captured(2), match.captured(3));
    }
    constexpr int kMaxChars = 16;
    if (version.size() > kMaxChars) {
        return version.left(kMaxChars - 1) + QChar(0x2026);
    }
    return version;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : PanelDialog(QStringLiteral("settingsDialog"), QStringLiteral("Settings"), parent)
{
    titleRow()->addWidget(label(QStringLiteral("Settings"), "helpTitle"), 0, Qt::AlignBaseline);

    // Caja del update: solo cuando un chequeo encontro una version nueva. La descarga y la
    // instalacion siguen en los carteles de lgaupdate.
    m_updateBox = new QFrame(this);
    m_updateBox->setObjectName(QStringLiteral("updateBox"));
    auto *updateLayout = new QHBoxLayout(m_updateBox);
    updateLayout->setContentsMargins(12, 11, 12, 11);
    updateLayout->setSpacing(12);
    m_dot = new QLabel(m_updateBox);
    m_dot->setFixedSize(8, 8);
    updateLayout->addWidget(m_dot, 0, Qt::AlignVCenter);
    auto *texts = new QVBoxLayout();
    texts->setSpacing(2);
    m_updateTitle = new QLabel(m_updateBox);
    m_updateTitle->setObjectName(QStringLiteral("updateTitle"));
    m_updateCaption = new QLabel(m_updateBox);
    m_updateCaption->setObjectName(QStringLiteral("caption"));
    m_updateCaption->setWordWrap(true);
    texts->addWidget(m_updateTitle);
    texts->addWidget(m_updateCaption);
    updateLayout->addLayout(texts, 1);
    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_later = Ui::button(QStringLiteral("Later"), QStringLiteral("ghost"), QStringLiteral("sm"), m_updateBox);
    m_install = Ui::button(QStringLiteral("Update"), QStringLiteral("primary"), QStringLiteral("sm"), m_updateBox);
    buttons->addWidget(m_later);
    buttons->addWidget(m_install);
    updateLayout->addLayout(buttons);
    body()->addWidget(m_updateBox);

    // Fila "Updates": como se chequea y boton para chequear ahora.
    auto *updatesRow = keyValueRow(QStringLiteral("Updates"), "kvKey");
    m_checkStatus = label(QString(), "kvValue");
    m_checkStatus->setWordWrap(true);
    m_check = Ui::button(QStringLiteral("Check now"), QString(), QStringLiteral("sm"), this);
    Ui::setIcon(m_check, Icon::Retry, Theme::color(Theme::kText));
    updatesRow->addWidget(m_checkStatus, 1);
    updatesRow->addWidget(m_check, 0, Qt::AlignVCenter);
    body()->addLayout(updatesRow);

    addRule(body());

    // Extension de navegador: como cargarla (sin Web Store, carpeta junto a la app).
    const auto strong = [](const QString &text) {
        return QStringLiteral("<span style=\"color:#F2F2F4;\">%1</span>").arg(text.toHtmlEscaped());
    };
    auto *extensionRow = new QHBoxLayout();
    extensionRow->setSpacing(12);
    extensionRow->addWidget(label(QStringLiteral("Browser extension"), "kvKeyStrong"), 0, Qt::AlignVCenter);
    extensionRow->addStretch(1);
    auto *openExtension = Ui::button(QStringLiteral("Open extension folder"), QString(), QStringLiteral("sm"), this);
    extensionRow->addWidget(openExtension, 0, Qt::AlignVCenter);
    body()->addLayout(extensionRow);
    auto *extensionIntro = label(QStringLiteral("Send the page you're on to the queue with one click, using your "
                                                "browser session."), "helpBody");
    extensionIntro->setWordWrap(true);
    body()->addWidget(extensionIntro);
    const QStringList steps = {
        QStringLiteral("Open %1 (Chrome: %2).").arg(strong(QStringLiteral("brave://extensions")),
                                                    strong(QStringLiteral("chrome://extensions"))),
        QStringLiteral("Turn on %1. Keep it on, or the extension stops.").arg(strong(QStringLiteral("Developer mode"))),
        QStringLiteral("Click %1 and pick the extension folder.").arg(strong(QStringLiteral("Load unpacked"))),
        QStringLiteral("Pin LGA Video Downloader in the toolbar."),
    };
    // Un QLabel de UNA linea por paso, sin wordWrap. Con wordWrap (heightForWidth) la ventana real
    // les daba menos alto que el texto y quedaban de 9 px, encimados; el render offscreen de
    // --ui-shot no lo reproducia. Por eso los textos son cortos: cada uno entra en el ancho fijo.
    auto *stepsLayout = new QVBoxLayout();
    stepsLayout->setSpacing(4);
    for (int i = 0; i < steps.size(); ++i) {
        auto *text = label(QStringLiteral("%1.&nbsp;&nbsp;%2").arg(i + 1).arg(steps.at(i)), "helpBody");
        text->setTextFormat(Qt::RichText);
        stepsLayout->addWidget(text);
    }
    body()->addLayout(stepsLayout);

    addRule(body());

    // Versiones instaladas de las tools. Los creditos de cada una estan en Help.
    body()->addWidget(label(QStringLiteral("Download tools"), "kvKeyStrong"));
    struct Tool { const char *name; QLabel **version; };
    const Tool tools[] = {
        {"yt-dlp", &m_ytdlpVersion},
        {"FFmpeg", &m_ffmpegVersion},
        {"Deno", &m_denoVersion},
    };
    auto *toolsLayout = new QVBoxLayout();
    toolsLayout->setSpacing(6);
    for (const Tool &tool : tools) {
        auto *row = keyValueRow(QString::fromLatin1(tool.name), "kvKey");
        *tool.version = label(QString(), "kvValue");
        row->addWidget(*tool.version, 1);
        toolsLayout->addLayout(row);
    }
    body()->addLayout(toolsLayout);
    body()->addWidget(label(QStringLiteral("yt-dlp and Deno update themselves. FFmpeg comes with the app."), "caption"));

    finish();
    connect(m_later, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_check, &QPushButton::clicked, this, &SettingsDialog::checkRequested);
    connect(m_install, &QPushButton::clicked, this, &SettingsDialog::installRequested);
    connect(openExtension, &QPushButton::clicked, this, &SettingsDialog::openExtensionFolderRequested);

    setToolVersions({});
    setUpdateView(UpdateView());
}

void SettingsDialog::setToolVersions(const QMap<QString, QString> &versions)
{
    const auto show = [&versions](QLabel *target, const char *key) {
        const QString value = versions.value(QLatin1String(key));
        if (value.isEmpty()) {
            target->setText(QStringLiteral("not found"));
            target->setToolTip(QString());
            return;
        }
        const QString shown = shortToolVersion(value);
        target->setText(shown);
        target->setToolTip(shown == value ? QString() : value);
    };
    show(m_ytdlpVersion, "yt-dlp");
    show(m_ffmpegVersion, "ffmpeg");
    show(m_denoVersion, "deno");
}

void SettingsDialog::setUpdateView(const UpdateView &view)
{
    // Sin hora del ultimo chequeo: lgaupdate no informa si termino bien o fallo, y "Last checked"
    // despues de un chequeo sin red diria algo que no paso. El resultado de un Check now lo
    // muestra su cartel.
    QString status = QStringLiteral("Checked automatically every few hours");
    if (!view.blockedReason.isEmpty()) {
        status = view.blockedReason;
    } else if (view.checking) {
        status = QStringLiteral("Checking for updates…");
    }

    const bool box = !view.availableVersion.isEmpty();
    m_updateBox->setVisible(box);
    m_dot->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 4px;").arg(QLatin1String(Theme::kAccent)));
    m_updateTitle->setText(QStringLiteral("Update available · v%1").arg(view.availableVersion));
#ifdef Q_OS_WIN
    // En Windows lgaupdate abre el wizard del instalador (no lo corre en silencio).
    const QString installCaption = QStringLiteral("Closes the app and opens the installer");
#else
    const QString installCaption = QStringLiteral("Closes the app, installs and opens it again");
#endif
    m_updateCaption->setText(!view.blockedReason.isEmpty() ? view.blockedReason : installCaption);
    m_install->setVisible(box);
    m_install->setEnabled(view.blockedReason.isEmpty() && !view.checking);
    // Enter ejecuta la accion principal y ese boton va siempre marcado: Update si se puede
    // instalar; si no, Close.
    const bool installIsEnter = box && m_install->isEnabled();
    Ui::setEnterButton(m_install, installIsEnter);
    Ui::setEnterButton(closeButton(), !installIsEnter);
    m_later->setVisible(box);
    m_checkStatus->setText(status);
    m_check->setEnabled(view.blockedReason.isEmpty() && !view.checking);
    if (!m_updateBox->property("accent").toBool()) {
        m_updateBox->setProperty("accent", true);
        Ui::repolish(m_updateBox);
    }
    fitHeight();
}

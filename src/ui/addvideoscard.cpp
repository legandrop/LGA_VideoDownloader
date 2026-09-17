#include "videodownloader/addvideoscard.h"
#include "videodownloader/theme.h"
#include "videodownloader/uiwidgets.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace {

constexpr int FORMAT_WIDTH = 170;
constexpr int QUALITY_WIDTH = 200;
constexpr int COOKIES_WIDTH = 200;

QVBoxLayout *column(const QString &label, QWidget *field, QWidget *parent)
{
    auto *layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    // El diseno separa 5px, pero la caja de linea del label en Qt mide 1px mas que en el
    // navegador (16 contra 15 para Inter 12.5px): con 4px el campo cae en la misma fila.
    layout->setSpacing(4);
    auto *caption = new QLabel(label, parent);
    caption->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(caption);
    if (field) {
        layout->addWidget(field);
    }
    return layout;
}

} // namespace

AddVideosCard::AddVideosCard(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 14);
    layout->setSpacing(0);

    // Encabezado
    auto *header = new QHBoxLayout();
    header->setContentsMargins(16, 12, 16, 0);
    header->setSpacing(10);
    auto *title = new QLabel(QStringLiteral("Add videos"), this);
    title->setObjectName(QStringLiteral("cardTitle"));
    auto *caption = new QLabel(QStringLiteral("Vimeo and YouTube"), this);
    caption->setObjectName(QStringLiteral("cardCaption"));
    header->addWidget(title);
    header->addWidget(caption);
    header->addStretch(1);
    layout->addLayout(header);

    // Links + Download
    auto *linksRow = new QHBoxLayout();
    linksRow->setContentsMargins(16, 10, 16, 0);
    linksRow->setSpacing(10);
    m_links = new QPlainTextEdit(this);
    m_links->setObjectName(QStringLiteral("urlInput"));
    m_links->setPlaceholderText(QStringLiteral("Paste one or more Vimeo or YouTube links, one per line…"));
    m_links->setFixedHeight(58);
    m_links->setTabChangesFocus(true);
    m_links->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_links->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_links->document()->setDocumentMargin(3);
    m_links->installEventFilter(this);
    m_download = Ui::button(QStringLiteral("Download"), QStringLiteral("primary"), QStringLiteral("big"), this);
    Ui::setIcon(m_download, Icon::ArrowDown, QColor(QStringLiteral("#DDDBEE")));
    m_download->setFixedWidth(150);
    m_download->setToolTip(QStringLiteral("Add the links to the queue and start downloading (Ctrl+Enter)"));
    linksRow->addWidget(m_links, 1);
    linksRow->addWidget(m_download, 0);
    layout->addLayout(linksRow);

    // Opciones
    auto *options = new QHBoxLayout();
    options->setContentsMargins(16, 10, 16, 0);
    options->setSpacing(10);

    m_format = new ArrowComboBox(this);
    m_format->addItem(QStringLiteral("MP4 · video + audio"), int(OutputFormat::VideoMp4));
    m_format->addItem(QStringLiteral("Audio only · M4A"), int(OutputFormat::AudioM4a));
    m_format->setFixedWidth(FORMAT_WIDTH);
    options->addLayout(column(QStringLiteral("Format"), m_format, this));

    m_quality = new ArrowComboBox(this);
    m_quality->addItem(QStringLiteral("Most compatible (H.264)"), int(VideoQuality::Compatible));
    m_quality->addItem(QStringLiteral("Best quality"), int(VideoQuality::Best));
    m_quality->setItemData(0, QStringLiteral("H.264 video that opens in any editor or player"), Qt::ToolTipRole);
    m_quality->setItemData(1, QStringLiteral("Highest resolution, often AV1 or VP9: some editors can't open it"), Qt::ToolTipRole);
    m_quality->setFixedWidth(QUALITY_WIDTH);
    options->addLayout(column(QStringLiteral("Quality"), m_quality, this));

    m_folderField = new QFrame(this);
    m_folderField->setObjectName(QStringLiteral("field"));
    m_folderField->setCursor(Qt::PointingHandCursor);
    m_folderField->installEventFilter(this);
    auto *folderLayout = new QHBoxLayout(m_folderField);
    folderLayout->setContentsMargins(8, 0, 8, 0);
    folderLayout->setSpacing(8);
    folderLayout->addWidget(new IconWidget(Icon::Folder, Theme::color(Theme::kIcon), 16, m_folderField));
    m_folder = new ElidedLabel(m_folderField);
    m_folder->setObjectName(QStringLiteral("fieldValue"));
    m_folder->setElideMode(Qt::ElideMiddle);
    folderLayout->addWidget(m_folder, 1);
    m_browse = Ui::button(QStringLiteral("Browse"), QString(), QString(), this);
    auto *folderRow = new QHBoxLayout();
    folderRow->setSpacing(6);
    folderRow->addWidget(m_folderField, 1);
    folderRow->addWidget(m_browse, 0);
    auto *saveTo = column(QStringLiteral("Save to"), nullptr, this);
    saveTo->addLayout(folderRow);
    options->addLayout(saveTo, 1);

    m_cookies = new ArrowComboBox(this);
    m_cookies->setObjectName(QStringLiteral("cookiesCombo"));
    m_cookies->setFixedWidth(COOKIES_WIDTH);
    options->addLayout(column(QStringLiteral("Use cookies from"), m_cookies, this));
    layout->addLayout(options);

    // Nota de la sesion
    auto *noteRow = new QHBoxLayout();
    noteRow->setContentsMargins(16, 8, 16, 0);
    noteRow->setSpacing(8);
    noteRow->addWidget(new IconWidget(Icon::Cookie, Theme::color(Theme::kIcon), 16, this), 0, Qt::AlignTop);
    m_note = new QLabel(this);
    m_note->setObjectName(QStringLiteral("note"));
    m_note->setTextFormat(Qt::RichText);
    m_note->setWordWrap(true);
    noteRow->addWidget(m_note, 1);
    layout->addLayout(noteRow);

    connect(m_download, &QPushButton::clicked, this, &AddVideosCard::downloadRequested);
    connect(m_browse, &QPushButton::clicked, this, &AddVideosCard::browseRequested);
    connect(m_cookies, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        emit cookiesSourceActivated(m_cookies->itemData(index).toString());
    });
    connect(m_format, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        // Solo audio: la calidad de video no aplica.
        m_quality->setEnabled(format() == OutputFormat::VideoMp4);
        emit formatChanged(format());
    });
    connect(m_quality, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        emit qualityChanged(quality());
    });

    setBrowsers({});
}

bool AddVideosCard::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_links && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            && (key->modifiers() & Qt::ControlModifier)) {
            emit downloadRequested();
            return true;
        }
    }
    if (watched == m_folderField && event->type() == QEvent::MouseButtonRelease) {
        emit browseRequested();
        return true;
    }
    return QFrame::eventFilter(watched, event);
}

QStringList AddVideosCard::links() const
{
    QStringList result;
    const QStringList lines = m_links->toPlainText().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        // Varios links pegados en un mismo renglon separados por espacios tambien valen.
        const QStringList words = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &word : words) {
            const QString trimmed = word.trimmed();
            if (!trimmed.isEmpty()) {
                result.append(trimmed);
            }
        }
    }
    return result;
}

void AddVideosCard::setLinksText(const QString &text)
{
    m_links->setPlainText(text);
}

void AddVideosCard::clearLinks()
{
    m_links->clear();
}

void AddVideosCard::setDownloadFolder(const QString &path)
{
    m_folder->setText(QDir::toNativeSeparators(path));
}

void AddVideosCard::setBrowsers(const QList<BrowserDetect::Browser> &browsers)
{
    m_browsers = browsers;
    const QString current = cookiesSource();
    m_cookies->clear();
    m_cookies->addItem(QStringLiteral("None"), QString());
    m_cookies->setItemData(0, QStringLiteral("Public videos only"), Qt::ToolTipRole);
    auto *model = qobject_cast<QStandardItemModel *>(m_cookies->model());
    for (const BrowserDetect::Browser &browser : browsers) {
        if (browser.supported) {
            m_cookies->addItem(browser.recommended ? QStringLiteral("%1 (recommended)").arg(browser.name) : browser.name,
                               browser.key);
        } else {
            m_cookies->addItem(QStringLiteral("%1 · not supported on Windows").arg(browser.name), browser.key);
            if (model) {
                // Visible pero no elegible: el usuario entiende por que su navegador no sirve.
                model->item(m_cookies->count() - 1)->setEnabled(false);
            }
            m_cookies->setItemData(m_cookies->count() - 1,
                                   QStringLiteral("Windows encrypts this browser's cookies. Use Firefox or a cookies.txt file."),
                                   Qt::ToolTipRole);
        }
    }
    m_cookies->addItem(QStringLiteral("cookies.txt file…"), QStringLiteral("file"));
    m_cookies->setItemData(m_cookies->count() - 1, QStringLiteral("A cookies.txt file exported from your browser"),
                           Qt::ToolTipRole);
    setCookiesSource(current, QString());
    updateNote();
}

void AddVideosCard::setCookiesSource(const QString &key, const QString &cookiesFile)
{
    int index = m_cookies->findData(key);
    auto *model = qobject_cast<QStandardItemModel *>(m_cookies->model());
    if (index < 0 || (model && !model->item(index)->isEnabled())) {
        index = 0;
    }
    const int fileIndex = m_cookies->findData(QStringLiteral("file"));
    if (fileIndex >= 0) {
        m_cookies->setItemText(fileIndex, !cookiesFile.isEmpty() && key == QLatin1String("file")
                                              ? QStringLiteral("cookies.txt · %1").arg(QFileInfo(cookiesFile).fileName())
                                              : QStringLiteral("cookies.txt file…"));
    }
    m_cookies->setCurrentIndex(index);
    m_cookies->setToolTip(key == QLatin1String("file") ? QDir::toNativeSeparators(cookiesFile)
                                                       : QStringLiteral("Browser session used for private, members-only or age-restricted videos"));
}

QString AddVideosCard::cookiesSource() const
{
    return m_cookies->count() > 0 ? m_cookies->currentData().toString() : QString();
}

void AddVideosCard::setFormat(OutputFormat value)
{
    m_format->setCurrentIndex(m_format->findData(int(value)));
    m_quality->setEnabled(value == OutputFormat::VideoMp4);
}

OutputFormat AddVideosCard::format() const
{
    return static_cast<OutputFormat>(m_format->currentData().toInt());
}

void AddVideosCard::setQuality(VideoQuality value)
{
    m_quality->setCurrentIndex(m_quality->findData(int(value)));
}

VideoQuality AddVideosCard::quality() const
{
    return static_cast<VideoQuality>(m_quality->currentData().toInt());
}

void AddVideosCard::updateNote()
{
    // Una sola linea: la aclaracion de Windows solo aparece si hay un navegador que no sirve.
    QString text = QStringLiteral("<span style=\"color:#9a9a9a; font-weight:500;\">No passwords needed.</span> "
                                  "Pick a browser where you are signed in to get private, members-only or age-restricted videos.");
    bool unsupported = false;
    for (const BrowserDetect::Browser &browser : std::as_const(m_browsers)) {
        unsupported = unsupported || !browser.supported;
    }
    if (unsupported) {
        text += QStringLiteral(" On Windows use Firefox or a cookies.txt file.");
    }
    m_note->setText(text);
}

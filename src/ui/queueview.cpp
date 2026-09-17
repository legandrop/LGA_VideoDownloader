#include "videodownloader/queueview.h"
#include "videodownloader/theme.h"
#include "videodownloader/uiwidgets.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QString humanSize(qint64 bytes)
{
    if (bytes <= 0) {
        return QString();
    }
    const double mib = bytes / (1024.0 * 1024.0);
    if (mib >= 1024.0) {
        return QString::number(mib / 1024.0, 'f', 2) + QStringLiteral(" GiB");
    }
    if (mib < 1.0) {
        return QString::number(bytes / 1024.0, 'f', 0) + QStringLiteral(" KiB");
    }
    return QString::number(mib, 'f', mib >= 100 ? 0 : 1) + QStringLiteral(" MiB");
}

QString humanEta(int seconds)
{
    if (seconds < 0) {
        return QString();
    }
    if (seconds >= 3600) {
        return QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
                   .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString ordinal(int n)
{
    const int mod100 = n % 100;
    const char *suffix = "th";
    if (mod100 < 11 || mod100 > 13) {
        switch (n % 10) {
        case 1: suffix = "st"; break;
        case 2: suffix = "nd"; break;
        case 3: suffix = "rd"; break;
        default: break;
        }
    }
    return QString::number(n) + QLatin1String(suffix);
}

// Link sin esquema ni "www." para mostrar mientras no hay titulo.
QString shortUrl(const QString &url)
{
    QString text = url.trimmed();
    for (const char *prefix : {"https://", "http://"}) {
        if (text.startsWith(QLatin1String(prefix), Qt::CaseInsensitive)) {
            text = text.mid(int(qstrlen(prefix)));
        }
    }
    if (text.startsWith(QLatin1String("www."), Qt::CaseInsensitive)) {
        text = text.mid(4);
    }
    return text;
}

// Fragmentos del renglon de datos separados como en el diseno (12px entre grupos).
QString joinMeta(const QStringList &parts)
{
    return parts.join(QStringLiteral("&nbsp;&nbsp;&nbsp;"));
}

QString strong(const QString &text)
{
    return QStringLiteral("<span style=\"color:#B2B2B2; font-weight:500;\">%1</span>").arg(text.toHtmlEscaped());
}

} // namespace

// ------------------------------------------------------------------ QueueCard

QueueCard::QueueCard(int id, QWidget *parent)
    : QFrame(parent)
    , m_id(id)
{
    setObjectName(QStringLiteral("tile"));

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(12, 10, 12, 10);
    row->setSpacing(12);

    m_badge = new StatusBadge(this);
    row->addWidget(m_badge, 0, Qt::AlignTop);

    auto *info = new QVBoxLayout();
    info->setSpacing(5);
    info->setContentsMargins(0, 0, 0, 0);

    auto *top = new QHBoxLayout();
    top->setSpacing(8);
    m_title = new ElidedLabel(this);
    m_title->setObjectName(QStringLiteral("itemTitle"));
    m_title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_source = new Chip(this);
    m_stateChip = new Chip(this);
    top->addWidget(m_title, 0);
    top->addWidget(m_source, 0);
    top->addWidget(m_stateChip, 0);
    top->addStretch(1);
    info->addLayout(top);

    m_progressRow = new QWidget(this);
    auto *progressLayout = new QHBoxLayout(m_progressRow);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressLayout->setSpacing(10);
    m_progress = new ProgressLine(8, m_progressRow);
    m_percent = new QLabel(m_progressRow);
    m_percent->setObjectName(QStringLiteral("percent"));
    progressLayout->addWidget(m_progress, 1);
    progressLayout->addWidget(m_percent, 0);
    info->addWidget(m_progressRow);

    m_meta = new QLabel(this);
    m_meta->setObjectName(QStringLiteral("meta"));
    m_meta->setTextFormat(Qt::RichText);
    // Sin esto el ancho minimo del label es el de su texto: una ruta larga ensancha la
    // tarjeta por debajo del boton Show y de la barra de scroll.
    m_meta->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    info->addWidget(m_meta);

    m_headline = new QLabel(this);
    m_headline->setObjectName(QStringLiteral("errorHeadline"));
    m_headline->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    info->addWidget(m_headline);

    m_fixBox = new QFrame(this);
    m_fixBox->setObjectName(QStringLiteral("fixBox"));
    auto *fixLayout = new QVBoxLayout(m_fixBox);
    fixLayout->setContentsMargins(11, 9, 11, 9);
    fixLayout->setSpacing(8);
    m_fixText = new QLabel(m_fixBox);
    m_fixText->setObjectName(QStringLiteral("fixText"));
    m_fixText->setWordWrap(true);
    m_fixText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fixLayout->addWidget(m_fixText);
    auto *fixButtons = new QHBoxLayout();
    fixButtons->setSpacing(6);
    m_retryBrowser = Ui::button(QStringLiteral("Retry with Firefox"), QStringLiteral("primary"), QStringLiteral("sm"), m_fixBox);
    Ui::setIcon(m_retryBrowser, Icon::Retry, QColor(QStringLiteral("#DDDBEE")));
    m_retry = Ui::button(QStringLiteral("Retry"), QStringLiteral("primary"), QStringLiteral("sm"), m_fixBox);
    Ui::setIcon(m_retry, Icon::Retry, QColor(QStringLiteral("#DDDBEE")));
    m_copyError = Ui::button(QStringLiteral("Copy error"), QString(), QStringLiteral("sm"), m_fixBox);
    fixButtons->addWidget(m_retryBrowser);
    fixButtons->addWidget(m_retry);
    fixButtons->addWidget(m_copyError);
    fixButtons->addStretch(1);
    fixLayout->addLayout(fixButtons);
    info->addWidget(m_fixBox);

    row->addLayout(info, 1);

    auto *actions = new QHBoxLayout();
    m_actions = actions;
    actions->setSpacing(4);
    m_show = Ui::button(QStringLiteral("Show"), QStringLiteral("ghost"), QStringLiteral("sm"), this);
    Ui::setIcon(m_show, Icon::Folder, Theme::color(Theme::kTextMuted), 16);
    m_show->setToolTip(QStringLiteral("Show the file in its folder"));
    m_cancel = Ui::button(QString(), QString(), QStringLiteral("icon"), this);
    Ui::setIcon(m_cancel, Icon::X, QColor(0x9a, 0x9a, 0x9a));
    m_cancel->setToolTip(QStringLiteral("Cancel"));
    m_remove = Ui::button(QString(), QString(), QStringLiteral("icon"), this);
    Ui::setIcon(m_remove, Icon::X, QColor(0x9a, 0x9a, 0x9a));
    m_remove->setToolTip(QStringLiteral("Remove from the list"));
    actions->addWidget(m_show);
    actions->addWidget(m_cancel);
    actions->addWidget(m_remove);
    row->addLayout(actions);

    connect(m_cancel, &QPushButton::clicked, this, [this]() { emit cancelRequested(m_id); });
    connect(m_remove, &QPushButton::clicked, this, [this]() { emit removeRequested(m_id); });
    connect(m_retry, &QPushButton::clicked, this, [this]() { emit retryRequested(m_id); });
    connect(m_retryBrowser, &QPushButton::clicked, this, [this]() {
        emit retryWithBrowserRequested(m_id, QStringLiteral("firefox"));
    });
    connect(m_show, &QPushButton::clicked, this, [this]() { emit showRequested(m_id); });
    connect(m_copyError, &QPushButton::clicked, this, [this]() { emit copyErrorRequested(m_id); });
}

void QueueCard::setState(const QString &state)
{
    if (state == m_state) {
        return;
    }
    m_state = state;
    setProperty("state", state);
    Ui::repolish(this);
}

void QueueCard::setItem(const DownloadItem &item, int position, bool firefoxAvailable)
{
    m_title->setText(item.title.isEmpty() ? shortUrl(item.url) : item.title);

    const QString source = item.isYouTube() ? QStringLiteral("YouTube")
                           : item.isVimeo() ? QStringLiteral("Vimeo") : QString();
    m_source->set(QStringLiteral("src"), source);
    m_source->setVisible(!source.isEmpty());

    const bool running = item.status == DownloadStatus::Downloading;
    const bool failed = item.status == DownloadStatus::Failed;
    const bool done = item.status == DownloadStatus::Completed;
    const bool cancelled = item.status == DownloadStatus::Cancelled;
    const bool pending = item.status == DownloadStatus::Pending;

    setState(running ? QStringLiteral("run") : failed ? QStringLiteral("err") : QString());
    m_badge->setKind(running ? StatusBadge::Kind::Running
                     : done ? StatusBadge::Kind::Done
                     : failed ? StatusBadge::Kind::Error : StatusBadge::Kind::Waiting);
    // Con error la tarjeta crece hacia abajo: insignia y acciones quedan arriba, como en el diseno.
    layout()->setAlignment(m_badge, failed ? Qt::AlignTop : Qt::AlignVCenter);
    layout()->setAlignment(m_actions, failed ? Qt::AlignTop : Qt::AlignVCenter);

    // Chip de estado: solo cuando aporta algo que la tarjeta no dice ya.
    if (failed) {
        const bool signIn = item.failure == FailureKind::NeedsSignIn || item.failure == FailureKind::CookiesUnreadable;
        m_stateChip->set(QStringLiteral("err"), signIn ? QStringLiteral("Needs sign-in") : QStringLiteral("Failed"));
        m_stateChip->show();
    } else if (cancelled) {
        m_stateChip->set(QString(), QStringLiteral("Cancelled"));
        m_stateChip->show();
    } else {
        m_stateChip->hide();
    }

    m_progressRow->setVisible(running);
    if (running) {
        m_progress->setValue(item.progress / 100.0, ProgressLine::Tone::Active);
        m_percent->setText(QStringLiteral("%1%").arg(item.progress));
    }

    QStringList meta;
    const QString format = item.resolution.isEmpty()
        ? (item.extension.isEmpty() ? QString() : item.extension)
        : QStringLiteral("%1 · %2").arg(item.resolution, item.extension);
    if (running) {
        if (item.finishing) {
            meta << QStringLiteral("Finishing · %1").arg(item.options.format == OutputFormat::AudioM4a
                                                              ? QStringLiteral("converting the audio")
                                                              : QStringLiteral("joining video and audio"));
        } else if (item.totalBytes > 0) {
            meta << (item.doneBytes >= 0 ? strong(humanSize(item.doneBytes)) + QStringLiteral(" of ") + humanSize(item.totalBytes)
                                         : strong(humanSize(item.totalBytes)));
            if (item.speedBytes > 0) {
                meta << humanSize(qint64(item.speedBytes)) + QStringLiteral("/s");
            }
            if (item.etaSeconds >= 0) {
                meta << QStringLiteral("ETA ") + humanEta(item.etaSeconds);
            }
        } else {
            meta << QStringLiteral("Fetching info…");
        }
        if (!format.isEmpty() && !item.finishing) {
            meta << format.toHtmlEscaped();
        }
    } else if (done) {
        if (item.totalBytes > 0) {
            meta << strong(humanSize(item.totalBytes));
        }
        if (!format.isEmpty()) {
            meta << format.toHtmlEscaped();
        }
        // Solo el nombre de la carpeta: la ruta completa va en el tooltip.
        const QString folder = QFileInfo(QDir::cleanPath(item.options.downloadDir)).fileName();
        meta << QStringLiteral("Saved to %1").arg((folder.isEmpty() ? QDir::toNativeSeparators(item.options.downloadDir) : folder).toHtmlEscaped());
    } else if (pending) {
        if (position < 0) {
            meta << QStringLiteral("Waiting for download tools");
        } else {
            meta << (position == 1 ? QStringLiteral("Up next") : QStringLiteral("Queued · %1 in line").arg(ordinal(position)));
        }
    } else if (cancelled) {
        meta << QStringLiteral("Cancelled before it finished");
    } else if (failed && !item.title.isEmpty()) {
        meta << shortUrl(item.url).toHtmlEscaped();
    }
    m_meta->setText(joinMeta(meta));
    m_meta->setVisible(!meta.isEmpty());
    m_meta->setToolTip(done ? QDir::toNativeSeparators(item.filePath.isEmpty() ? item.options.downloadDir : item.filePath)
                            : QString());

    m_headline->setVisible(failed);
    m_fixBox->setVisible(failed);
    if (failed) {
        m_headline->setText(item.errorHeadline);
        m_fixText->setText(item.errorDetail);
        const bool sessionProblem = item.failure == FailureKind::NeedsSignIn || item.failure == FailureKind::CookiesUnreadable;
        const bool usingFirefox = item.options.cookiesBrowser == QLatin1String("firefox");
        const bool retryable = item.failure != FailureKind::InvalidLink;
        // Error de sesion con Firefox disponible: el arreglo es un boton. Si no, Retry comun
        // (el usuario puede haber cambiado Use cookies from antes de reintentar).
        const bool offerFirefox = sessionProblem && firefoxAvailable && !usingFirefox;
        m_retryBrowser->setVisible(offerFirefox);
        m_retry->setVisible(retryable && !offerFirefox);
        m_copyError->setVisible(!item.errorMessage.isEmpty());
    }

    m_show->setVisible(done);
    m_cancel->setVisible(running || pending);
    m_remove->setVisible(failed || cancelled);
}

// ------------------------------------------------------------------ QueueView

QueueView::QueueView(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));
    // Encabezado (29) + margenes de la lista (20) + una tarjeta de error con su caja de
    // solucion y botones (140) + pie (47): la tarjeta entra completa aun con la ventana minima.
    setMinimumHeight(236);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(16, 12, 16, 0);
    header->setSpacing(10);
    auto *title = new QLabel(QStringLiteral("Queue"), this);
    title->setObjectName(QStringLiteral("cardTitle"));
    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("cardCaption"));
    header->addWidget(title);
    header->addWidget(m_count);
    header->addStretch(1);
    layout->addLayout(header);

    m_stack = new QStackedWidget(this);

    // Estado vacio
    auto *empty = new QWidget(m_stack);
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setSpacing(8);
    emptyLayout->addStretch(1);
    auto *circle = new QFrame(empty);
    circle->setFixedSize(44, 44);
    circle->setStyleSheet(QStringLiteral("QFrame { background-color: #242424; border-radius: 22px; }"));
    auto *circleLayout = new QHBoxLayout(circle);
    circleLayout->setContentsMargins(0, 0, 0, 0);
    circleLayout->addWidget(new IconWidget(Icon::Link, Theme::color(Theme::kIcon), 16, circle), 0, Qt::AlignCenter);
    emptyLayout->addWidget(circle, 0, Qt::AlignHCenter);
    emptyLayout->addSpacing(4);
    auto *emptyTitle = new QLabel(QStringLiteral("The queue is empty"), empty);
    emptyTitle->setObjectName(QStringLiteral("emptyTitle"));
    auto *emptyText = new QLabel(QStringLiteral("Paste links above and press Download. Each link becomes a row here."), empty);
    emptyText->setObjectName(QStringLiteral("emptyText"));
    emptyLayout->addWidget(emptyTitle, 0, Qt::AlignHCenter);
    emptyLayout->addWidget(emptyText, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(1);
    m_stack->addWidget(empty);

    // Lista
    m_scroll = new QScrollArea(m_stack);
    m_scroll->setObjectName(QStringLiteral("queueScroll"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // El viewport de un QScrollArea pinta el color Base de la paleta si no se le dice nada:
    // ese es el clasico rectangulo de otro color dentro de la tarjeta.
    m_scroll->viewport()->setObjectName(QStringLiteral("queueViewport"));
    m_scroll->viewport()->setAutoFillBackground(false);
    m_listHost = new QWidget();
    m_listHost->setObjectName(QStringLiteral("queueList"));
    m_listHost->setAutoFillBackground(false);
    m_listLayout = new QVBoxLayout(m_listHost);
    m_listLayout->setContentsMargins(12, 10, 12, 10);
    m_listLayout->setSpacing(6);
    m_listLayout->addStretch(1);
    m_scroll->setWidget(m_listHost);
    m_stack->addWidget(m_scroll);
    layout->addWidget(m_stack, 1);

    // Pie
    m_footer = new QFrame(this);
    m_footer->setObjectName(QStringLiteral("queueFooter"));
    auto *footer = new QHBoxLayout(m_footer);
    footer->setContentsMargins(16, 10, 16, 10);
    footer->setSpacing(12);
    m_overallLabel = new QLabel(QStringLiteral("Overall"), m_footer);
    m_overallLabel->setObjectName(QStringLiteral("footerLabel"));
    m_overall = new ProgressLine(10, m_footer);
    m_overallValue = new QLabel(m_footer);
    m_overallValue->setObjectName(QStringLiteral("footerValue"));
    m_attention = new QLabel(m_footer);
    m_attention->setObjectName(QStringLiteral("footerAlert"));
    m_clearFinished = Ui::button(QStringLiteral("Clear finished"), QStringLiteral("ghost"), QStringLiteral("sm"), m_footer);
    m_retryFailed = Ui::button(QStringLiteral("Retry failed"), QString(), QStringLiteral("sm"), m_footer);
    Ui::setIcon(m_retryFailed, Icon::Retry, Theme::color(Theme::kText));
    m_cancelAll = Ui::button(QStringLiteral("Cancel all"), QString(), QStringLiteral("sm"), m_footer);
    footer->addWidget(m_overallLabel);
    footer->addWidget(m_overall, 1);
    footer->addWidget(m_overallValue);
    footer->addWidget(m_attention);
    footer->addStretch(0);
    footer->addWidget(m_clearFinished);
    footer->addWidget(m_retryFailed);
    footer->addWidget(m_cancelAll);
    layout->addWidget(m_footer);

    connect(m_clearFinished, &QPushButton::clicked, this, &QueueView::clearFinishedRequested);
    connect(m_retryFailed, &QPushButton::clicked, this, &QueueView::retryFailedRequested);
    connect(m_cancelAll, &QPushButton::clicked, this, &QueueView::cancelAllRequested);

    refresh();
}

QSize QueueView::sizeHint() const
{
    return QSize(QFrame::sizeHint().width(), minimumHeight());
}

void QueueView::upsertItem(const DownloadItem &item)
{
    if (!m_items.contains(item.id)) {
        m_order.append(item.id);
        auto *card = new QueueCard(item.id, m_listHost);
        m_cards.insert(item.id, card);
        m_listLayout->insertWidget(m_listLayout->count() - 1, card);
        connect(card, &QueueCard::cancelRequested, this, &QueueView::cancelRequested);
        connect(card, &QueueCard::removeRequested, this, &QueueView::removeRequested);
        connect(card, &QueueCard::retryRequested, this, &QueueView::retryRequested);
        connect(card, &QueueCard::retryWithBrowserRequested, this, &QueueView::retryWithBrowserRequested);
        connect(card, &QueueCard::showRequested, this, &QueueView::showRequested);
        connect(card, &QueueCard::copyErrorRequested, this, &QueueView::copyErrorRequested);
    }
    const bool known = m_items.contains(item.id);
    const DownloadStatus previous = known ? m_items.value(item.id).status : item.status;
    m_items.insert(item.id, item);
    if (known && previous == DownloadStatus::Downloading && item.status == DownloadStatus::Downloading) {
        // Solo progreso del item actual: se toca su tarjeta y el pie, no toda la lista.
        m_cards.value(item.id)->setItem(item, 0, m_firefoxAvailable);
        refreshSummary();
    } else {
        // Cambio de estado: pueden moverse los lugares en cola de todos.
        refresh();
    }
}

void QueueView::removeItem(int id)
{
    m_items.remove(id);
    m_order.removeAll(id);
    if (QueueCard *card = m_cards.take(id)) {
        card->deleteLater();
    }
    refresh();
}

void QueueView::setFirefoxAvailable(bool available)
{
    m_firefoxAvailable = available;
    refresh();
}

void QueueView::setWaitingForTools(bool waiting)
{
    m_waitingForTools = waiting;
    refresh();
}

void QueueView::refresh()
{
    int position = 0;
    for (int id : std::as_const(m_order)) {
        const DownloadItem item = m_items.value(id);
        int slot = 0;
        if (item.status == DownloadStatus::Pending) {
            ++position;
            slot = m_waitingForTools ? -1 : position;
        }
        if (QueueCard *card = m_cards.value(id)) {
            card->setItem(item, slot, m_firefoxAvailable);
        }
    }
    refreshSummary();
}

void QueueView::refreshSummary()
{
    int done = 0, failed = 0, running = 0, pending = 0, cancelled = 0, retryable = 0;
    double progressSum = 0.0;
    for (int id : std::as_const(m_order)) {
        const DownloadItem &item = *m_items.constFind(id);
        switch (item.status) {
        case DownloadStatus::Completed: ++done; progressSum += 1.0; break;
        case DownloadStatus::Failed:
            ++failed;
            progressSum += 1.0;
            if (item.failure != FailureKind::InvalidLink) {
                ++retryable;
            }
            break;
        case DownloadStatus::Cancelled: ++cancelled; progressSum += 1.0; break;
        case DownloadStatus::Downloading: ++running; progressSum += item.progress / 100.0; break;
        case DownloadStatus::Pending: ++pending; break;
        }
    }
    const int total = m_order.size();

    QStringList parts;
    parts << (total == 1 ? QStringLiteral("1 item") : QStringLiteral("%1 items").arg(total));
    if (done) parts << QStringLiteral("%1 done").arg(done);
    if (running) parts << QStringLiteral("%1 downloading").arg(running);
    if (pending) parts << QStringLiteral("%1 queued").arg(pending);
    if (failed) parts << QStringLiteral("%1 failed").arg(failed);
    if (cancelled) parts << QStringLiteral("%1 cancelled").arg(cancelled);
    m_count->setText(parts.join(QStringLiteral(" · ")));

    m_stack->setCurrentIndex(total == 0 ? 0 : 1);
    m_footer->setVisible(total > 0);

    const bool active = running + pending > 0;
    const int finished = done + failed + cancelled;
    m_overallLabel->setVisible(active);
    m_overall->setVisible(active);
    m_overallValue->setVisible(active);
    m_cancelAll->setVisible(active);
    if (active) {
        m_overall->setValue(total > 0 ? progressSum / total : 0.0, ProgressLine::Tone::Active);
        m_overallValue->setText(m_waitingForTools && running == 0
                                    ? QStringLiteral("Waiting for download tools")
                                    : QStringLiteral("%1 of %2 done").arg(finished).arg(total));
    }
    m_attention->setVisible(!active);
    if (!active) {
        const QString name = failed > 0 ? QStringLiteral("footerAlert") : QStringLiteral("footerLabel");
        if (m_attention->objectName() != name) {
            m_attention->setObjectName(name);
            Ui::repolish(m_attention);
        }
        if (failed > 0) {
            m_attention->setText(failed == 1 ? QStringLiteral("1 item needs attention")
                                             : QStringLiteral("%1 items need attention").arg(failed));
        } else {
            m_attention->setText(QStringLiteral("All downloads finished"));
        }
    }
    // Sin nada activo el mensaje empuja los botones a la derecha; con progreso, la barra.
    static_cast<QHBoxLayout *>(m_footer->layout())->setStretchFactor(m_attention, active ? 0 : 1);
    m_clearFinished->setEnabled(finished > 0);
    m_retryFailed->setVisible(!active && retryable > 0);
}

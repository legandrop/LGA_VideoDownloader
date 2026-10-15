#include "lgaupdate/LgaUpdateService.h"

#include "lgaupdate/LgaVersionCompare.h"
#include "lgaupdate/LgaWhatsNew.h"

#include "videodownloader/ConfirmDialog.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSettings>
#include <QSslError>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QRandomGenerator>
#include <QTimer>
#include <QVersionNumber>

#include <memory>

namespace {

// Reintentos de una descarga cortada, reanudando por Range desde donde quedo. Acotado a
// proposito: si la conexion esta rota de verdad, insistir solo demora el aviso al usuario.
constexpr int kMaxDownloadRetries = 3;

// "Remind me later" calla el chequeo automatico este tiempo. Era una semana: demasiado para una
// version que el usuario solo queria posponer.
constexpr int kSnoozeDays = 1;

// Cada cuanto mira el reloj el chequeo periodico. Corto a proposito: es lo que tarda en correr un
// chequeo vencido al despertar de una suspension. Mirar el reloj no consulta la red.
constexpr int kPeriodicTickMs = 5 * 60 * 1000;

// Hasta que version vio el usuario las notas ("What's new"). Va en el .ini de la app, junto al
// snooze y el skip.
constexpr const char *kWhatsNewLastSeenKey = "Updates/WhatsNewLastSeen";

// Version del formato del manifiesto que este componente sabe leer. Si LGA_Updates publica una
// mayor, el chequeo falla con un mensaje claro en vez de leer campos que cambiaron de sentido.
constexpr int kManifestSchemaVersion = 1;

// Datos del asset del release que se va a instalar.
struct ReleaseInfo {
    QString version;
    QUrl downloadUrl;
    QString assetName;
    QString assetDigest;
};

QString normalizedVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        version.remove(0, 1);
    }
    return version;
}

bool isVersionNewer(const QString &remoteVersion, const QString &localVersion)
{
    // Las apps Qt de LGA versionan `X.YYY`, de ahi el ancho de 3. La regla completa y el
    // porque del zero-padding estan en LgaVersionCompare.h.
    return LGA::Update::VersionCompare::isNewer(
        remoteVersion, localVersion, LGA::Update::VersionCompare::kMinorWidthThreeDigits);
}

QString normalizedSha256Digest(QString digest)
{
    digest = digest.trimmed();
    if (digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
        digest.remove(0, QStringLiteral("sha256:").size());
    }
    digest = digest.toLower();

    static const QRegularExpression digestPattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (!digestPattern.match(digest).hasMatch()) {
        return {};
    }
    return digest;
}

// El asset que corresponde a esta plataforma, por nombre. El patron se arma con el productId:
// "LGA_FrameRev_Mac_v0.216.zip" / "LGA_FrameRev_Setup_v0.216.exe".
QRegularExpression expectedAssetPattern(const QString &productId)
{
#ifdef Q_OS_MAC
    const QString pattern = QStringLiteral("^%1_Mac_v[0-9]+(?:\\.[0-9]+)+\\.zip$")
                                .arg(QRegularExpression::escape(productId));
#else
    const QString pattern = QStringLiteral("^%1_Setup_v[0-9]+(?:\\.[0-9]+)+\\.exe$")
                                .arg(QRegularExpression::escape(productId));
#endif
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
}

QString defaultAssetName(const QString &productId, const QString &version)
{
    const QString v = normalizedVersion(version);
#ifdef Q_OS_MAC
    return QStringLiteral("%1_Mac_v%2.zip").arg(productId, v);
#else
    return QStringLiteral("%1_Setup_v%2.exe").arg(productId, v);
#endif
}

// El nombre del asset viaja en el manifiesto, o sea que es entrada externa: si trae
// separadores de path podria escribir fuera de la carpeta de updates.
QString safeInstallerAssetName(QString assetName, const QString &productId, const QString &version)
{
    assetName = assetName.trimmed();
    if (assetName.isEmpty() || assetName.contains(QLatin1Char('/'))
        || assetName.contains(QLatin1Char('\\'))) {
        return defaultAssetName(productId, version);
    }
    return assetName;
}

QString currentPlatformLabel()
{
#ifdef Q_OS_MAC
    return QStringLiteral("macOS");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Windows");
#else
    return QStringLiteral("this platform");
#endif
}

QString currentMacAppBundlePath()
{
#ifdef Q_OS_MAC
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString bundlePath =
        QFileInfo(appDir.filePath(QStringLiteral("../.."))).canonicalFilePath();
    if (!bundlePath.isEmpty() && bundlePath.endsWith(QStringLiteral(".app"), Qt::CaseInsensitive)) {
        return bundlePath;
    }
#endif
    return {};
}

void logSslErrors(const QList<QSslError> &errors)
{
    QStringList descriptions;
    descriptions.reserve(errors.size());
    for (const QSslError &error : errors) {
        descriptions.append(error.errorString());
    }
    qWarning().noquote() << QStringLiteral("Updater: errores SSL; Qt cancelara la conexion: %1")
                                .arg(descriptions.join(QStringLiteral(" | ")));
}

// Arma la URL de descarga de un asset. Se DERIVA del slug, el tag y el nombre en vez de venir
// en el manifiesto: guardarla ahi seria un segundo lugar donde el mismo dato puede quedar viejo,
// y el formato de esa URL es publico y estable.
QUrl assetDownloadUrl(const QString &releaseRepoSlug, const QString &tag, const QString &name)
{
    const QStringList slugParts = releaseRepoSlug.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (slugParts.size() != 2 || tag.isEmpty() || name.isEmpty()) {
        return {};
    }
    return QUrl(QStringLiteral("https://github.com/%1/%2/releases/download/%3/%4")
                    .arg(slugParts.at(0), slugParts.at(1), tag, name));
}

// El asset de ESTA plataforma en el release MAS NUEVO que lo tenga, mirando `assetLatest`.
//
// Ese campo existe porque `tag`/`assets` describen el ULTIMO release del producto, y ese release
// puede no traer artefacto para esta plataforma. Elegir por posicion dejaria ciego a un
// usuario cuando el release mas nuevo solo contiene el instalador de la otra plataforma. Lo
// que corresponde ver es la ultima version QUE EXISTE PARA LA PLATAFORMA ACTUAL.
//
// El manifiesto no sabe nada de plataformas: publica, por producto, el asset mas nuevo de cada
// familia de artefacto. Quien decide cual le toca es la app, con el patron que ya tiene.
//
// Devuelve un info vacio si el manifiesto todavia no publica `assetLatest` (formato viejo) o si
// ninguna entrada matchea; el llamador cae al ultimo release en ese caso.
ReleaseInfo parseAssetLatest(const QJsonObject &product,
                             const QRegularExpression &assetPattern,
                             const QString &releaseRepoSlug)
{
    ReleaseInfo info;

    const QJsonArray entries = product.value(QStringLiteral("assetLatest")).toArray();
    for (const QJsonValue &entryValue : entries) {
        const QJsonObject entry = entryValue.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString();
        if (!assetPattern.match(name).hasMatch()) {
            continue;
        }

        const QString entryTag = entry.value(QStringLiteral("tag")).toString().trimmed();
        const QString entryVersion = normalizedVersion(entryTag);
        if (entryVersion.isEmpty()) {
            continue;
        }
        // No se confia en el orden del array: gana la version mas alta, la ordene el
        // manifiesto o no.
        if (!info.version.isEmpty() && !isVersionNewer(entryVersion, info.version)) {
            continue;
        }

        const QUrl url = assetDownloadUrl(releaseRepoSlug, entryTag, name);
        if (!url.isValid()) {
            continue;
        }

        info.version = entryVersion;
        info.assetName = name;
        info.downloadUrl = url;
        info.assetDigest = normalizedSha256Digest(entry.value(QStringLiteral("digest")).toString());
    }

    return info;
}

// Saca del manifiesto la version y el asset de ESTA plataforma para ESTE producto.
//
// No hay guarda contra draft/prerelease como cuando se leia la API de GitHub: el manifiesto se
// arma con releases que ya excluyen ambos.
ReleaseInfo parseManifest(const QByteArray &payload,
                          const QString &productId,
                          const QString &releaseRepoSlug,
                          QString *failureReason)
{
    if (failureReason) {
        failureReason->clear();
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (failureReason) {
            *failureReason = QStringLiteral("JSON invalido: %1 (offset %2)")
                                 .arg(parseError.errorString())
                                 .arg(parseError.offset);
        }
        return {};
    }

    const QJsonObject root = doc.object();
    // Se valida ANTES de leer nada: si el formato cambia, el chequeo falla de forma visible en
    // vez de malinterpretar campos que ya no significan lo mismo. `assetLatest` es ADITIVO y no
    // mueve este numero: los consumidores que solo leen `tag`/`assets` siguen andando igual.
    if (root.value(QStringLiteral("schemaVersion")).toInt(0) != kManifestSchemaVersion) {
        if (failureReason) {
            *failureReason =
                QStringLiteral("schemaVersion incompatible: recibido=%1 esperado=%2")
                    .arg(root.value(QStringLiteral("schemaVersion")).toVariant().toString())
                    .arg(kManifestSchemaVersion);
        }
        return {};
    }

    const QJsonObject product = root.value(QStringLiteral("products")).toObject()
                                    .value(releaseRepoSlug).toObject();
    if (product.isEmpty()) {
        return {};
    }

    const QRegularExpression assetPattern = expectedAssetPattern(productId);

    const ReleaseInfo byPlatform = parseAssetLatest(product, assetPattern, releaseRepoSlug);
    if (byPlatform.downloadUrl.isValid()) {
        return byPlatform;
    }

    // Manifiesto sin `assetLatest`: el ultimo release del producto, como se leia hasta ahora.
    // Si ese release no trae artefacto para esta plataforma, `downloadUrl` queda invalida y el
    // llamador lo dice con todas las letras en vez de callarse.
    const QString tag = product.value(QStringLiteral("tag")).toString().trimmed();
    ReleaseInfo info;
    info.version = normalizedVersion(tag);
    if (info.version.isEmpty()) {
        return info;
    }

    const QJsonArray assets = product.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &assetValue : assets) {
        const QJsonObject asset = assetValue.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (!assetPattern.match(name).hasMatch()) {
            continue;
        }

        const QUrl url = assetDownloadUrl(releaseRepoSlug, tag, name);
        if (!url.isValid()) {
            continue;
        }

        info.assetName = name;
        info.downloadUrl = url;
        info.assetDigest = normalizedSha256Digest(asset.value(QStringLiteral("digest")).toString());
        break;
    }

    return info;
}

// Acceso al .ini de snooze/skip. Con `path` vacio se cae al QSettings nativo, que es lo que
// hace la app cuando no se puede resolver el directorio de configuracion.
std::unique_ptr<QSettings> openUpdateSettings(const QString &path)
{
    if (path.isEmpty()) {
        return std::make_unique<QSettings>(QApplication::organizationName(),
                                           QApplication::applicationName());
    }
    return std::make_unique<QSettings>(path, QSettings::IniFormat);
}

} // namespace

LgaUpdateService::LgaUpdateService(LgaUpdateConfig config, QWidget *parentWindow)
    : QObject(parentWindow)
    , m_config(std::move(config))
    , m_parentWindow(parentWindow)
{
    if (m_config.networkFactory) {
        m_network = m_config.networkFactory(this);
    }
    if (m_network && m_network->thread() != thread()) {
        qWarning().noquote()
            << QStringLiteral("Updater: el manager de red recibido pertenece a otro hilo; "
                              "se usa el manager predeterminado");
        m_network = nullptr;
    }
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
    } else if (m_network->parent() != this) {
        m_network->setParent(this);
    }
}

LgaUpdateService::~LgaUpdateService()
{
    // Los fetchers de notas se borran ANTES que el network manager. Los dos son hijos de este
    // objeto y el manager se creo primero, asi que Qt lo destruiria primero, llevandose las
    // replies en vuelo; el fetcher, al morir despues, cancelaria una reply ya liberada (crash
    // al cerrar la app con una descarga de notas en curso). El QPointer del fetcher es la otra
    // mitad de la defensa: ninguna de las dos depende de la otra.
    const QList<LgaWhatsNewFetcher *> fetchers =
        findChildren<LgaWhatsNewFetcher *>(Qt::FindDirectChildrenOnly);
    qDeleteAll(fetchers);
    if (m_historyRunning) {
        QApplication::restoreOverrideCursor();
    }
    // Si la ventana muere con una descarga en curso, el parcial NO se commitea: se tira. Un
    // instalador a medio bajar en la carpeta de updates es peor que no tener ninguno.
    discardPartialDownload();
}

QWidget *LgaUpdateService::parentWindow() const
{
    QWidget *const owner = m_parentWindow.data();
    QWidget *const modal = QApplication::activeModalWidget();
    // isAncestorOf se limita a una misma ventana; Help es un dialogo con ventana propia.
    for (QWidget *parent = modal; owner && parent; parent = parent->parentWidget()) {
        if (parent == owner) return modal;
    }
    return owner;
}

bool LgaUpdateService::isBusy() const
{
    return m_checkReply || m_downloadReply || m_promptVisible || m_resultDialogVisible
        || m_notesPending || m_whatsNewDialogVisible;
}

void LgaUpdateService::scheduleAutomaticCheck()
{
    if (m_automaticCheckScheduled) {
        return;
    }
    m_automaticCheckScheduled = true;

    // Primero lo que se instalo sin verlo: si el chequeo dispara mientras este paso todavia
    // busca notas, se pospone hasta que termine (ver `checkForUpdates`), asi nunca se apilan
    // el "What's New" y el "Update Available".
    m_afterUpdateRunning = true;
    QTimer::singleShot(m_config.whatsNewAfterUpdateDelayMs, this,
                       [this]() { runWhatsNewAfterUpdate(); });
    QTimer::singleShot(m_config.automaticCheckDelayMs, this, [this]() { checkForUpdates(false); });

    // Despues del chequeo del arranque, el periodico: FrameRev vive en el tray dias enteros y no
    // se enteraba nunca de una version nueva.
    if (m_config.periodicCheckIntervalMs > 0) {
        schedulePeriodicCheckIn(m_config.periodicCheckIntervalMs, /*withJitter=*/true);
        m_periodicTimer = new QTimer(this);
        m_periodicTimer->setTimerType(Qt::VeryCoarseTimer);
        m_periodicTimer->setInterval(qMin(kPeriodicTickMs, m_config.periodicCheckIntervalMs));
        connect(m_periodicTimer, &QTimer::timeout, this, &LgaUpdateService::onPeriodicTick);
        m_periodicTimer->start();
    }

    // Red de seguridad: el chequeo automatico NUNCA puede quedar esperando para siempre a las
    // notas. Cada paso de red tiene su timeout, pero si alguno no terminara, sin esto la app
    // dejaria de ofrecer updates en silencio. Con el cartel de notas abierto no se fuerza
    // nada: al cerrarlo, el propio paso libera el chequeo.
    const int watchdogMs =
        m_config.whatsNewAfterUpdateDelayMs + 2 * m_config.checkTimeoutMs + 5000;
    QTimer::singleShot(watchdogMs, this, [this]() {
        if (m_afterUpdateRunning && !m_whatsNewDialogVisible) {
            qWarning().noquote()
                << QStringLiteral("Updater: el paso de notas no termino a tiempo; el chequeo "
                                  "automatico sigue sin esperarlo");
            finishWhatsNewAfterUpdate();
        }
    });
}

void LgaUpdateService::schedulePeriodicCheckIn(qint64 delayMs, bool withJitter)
{
    qint64 jitterMs = 0;
    if (withJitter && m_config.periodicCheckJitterMs > 0) {
        const int span = m_config.periodicCheckJitterMs;
        jitterMs = QRandomGenerator::global()->bounded(2 * span + 1) - span;
    }
    m_nextPeriodicCheckUtc =
        QDateTime::currentDateTimeUtc().addMSecs(qMax<qint64>(0, delayMs + jitterMs));
}

void LgaUpdateService::onPeriodicTick()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();

    // Un reloj que se atraso (a mano o por zona) dejaria el proximo chequeo a dias de distancia.
    const qint64 maxAheadMs =
        qint64(m_config.periodicCheckIntervalMs) + m_config.periodicCheckJitterMs;
    if (now.msecsTo(m_nextPeriodicCheckUtc) > maxAheadMs) {
        schedulePeriodicCheckIn(m_config.periodicCheckIntervalMs, /*withJitter=*/true);
        return;
    }
    if (now < m_nextPeriodicCheckUtc) {
        return;
    }

    // Con un cartel abierto, una descarga o un "What's new" en curso no se pisa nada: el tick
    // siguiente lo vuelve a intentar. El tick sigue corriendo dentro del event loop de un modal.
    if (isBusy() || m_afterUpdateRunning || m_historyRunning) {
        return;
    }

    schedulePeriodicCheckIn(m_config.periodicCheckIntervalMs, /*withJitter=*/true);
    m_periodicCheckActive = true;
    checkForUpdates(false);
    // Puede no haber arrancado nada (chequeo automatico deshabilitado, binario no actualizable).
    if (!m_checkReply) {
        m_periodicCheckActive = false;
    }
}

void LgaUpdateService::checkForUpdates(bool manual)
{
    if (!m_network) {
        return;
    }

    // El automatico no compite con un "What's new" en curso ni con un cartel de resultado
    // abierto (por ejemplo, "no se pudieron cargar las notas" del historial): se pospone y corre
    // cuando ese termina, en vez de perderse por `isBusy()` hasta el proximo chequeo.
    if (!manual && (m_afterUpdateRunning || m_historyRunning || m_whatsNewDialogVisible
                    || m_resultDialogVisible)) {
        m_automaticCheckDeferred = true;
        return;
    }

    if (isBusy()) {
        if (manual) {
            showResultDialog(tr("Updates"), tr("An update operation is already in progress."));
        }
        return;
    }

    // Antes que la preferencia del usuario: si este binario no se puede actualizar, el chequeo
    // no tiene a donde llegar, ni siquiera pedido a mano.
    if (m_config.updateBlockedReason) {
        const QString reason = m_config.updateBlockedReason();
        if (!reason.isEmpty()) {
            if (manual) {
                showResultDialog(tr("Updates"), reason);
            }
            return;
        }
    }

    if (!manual && m_config.automaticChecksEnabled && !m_config.automaticChecksEnabled()) {
        return;
    }

    m_manualCheckActive = manual;
    startCheckRequest();
}

void LgaUpdateService::startCheckRequest()
{
    QNetworkRequest request{QUrl(m_config.manifestUrl)};
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("%1/%2").arg(m_config.productId, QApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/json");
    // Pages ya sirve el manifiesto por CDN con su propio cache; el de Qt encima solo agregaria
    // una segunda capa que hace mas dificil saber que version se esta leyendo.
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    // Sin timeout una conexion colgada deja el flujo esperando para siempre.
    request.setTransferTimeout(m_config.checkTimeoutMs);

    m_checkReply = m_network->get(request);
    m_checkPhaseOpen = true;
    emit checkStarted();
    connect(m_checkReply, &QNetworkReply::finished, this, &LgaUpdateService::onCheckFinished);
    connect(m_checkReply, &QNetworkReply::sslErrors, this,
            [](const QList<QSslError> &errors) { logSslErrors(errors); });
}

void LgaUpdateService::endCheckPhase()
{
    if (m_checkPhaseOpen) {
        m_checkPhaseOpen = false;
        emit checkFinished();
    }
}

void LgaUpdateService::onCheckFinished()
{
    QNetworkReply *reply = m_checkReply;
    m_checkReply = nullptr;

    // `checkFinished()` sale una sola vez por chequeo, por `endCheckPhase()`:
    //  - en los desenlaces con cartel, ANTES del cartel (lo hace `showResultDialog`);
    //  - en los que terminan callados, al salir de esta funcion (este guard);
    //  - cuando hay update, al terminar la espera de las notas (`promptWithNotes`), para que el
    //    boton siga en "Checking..." mientras tanto.
    bool waitingForNotes = false;
    const auto checkPhaseGuard = qScopeGuard([this, &waitingForNotes]() {
        if (!waitingForNotes) {
            endCheckPhase();
        }
    });

    if (!reply) {
        return;
    }

    const bool manual = m_manualCheckActive;
    const bool periodic = m_periodicCheckActive;
    m_manualCheckActive = false;
    m_periodicCheckActive = false;

    const QNetworkReply::NetworkError error = reply->error();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    // El chequeo automatico se calla en TODOS los desenlaces que no son "hay version nueva":
    // no interrumpe al usuario para decirle que esta al dia.
    if (error != QNetworkReply::NoError) {
        // Al despertar de una suspension la red suele tardar en levantar: el periodico que falla
        // se reintenta pronto en vez de esperar un intervalo entero.
        if (periodic && m_config.periodicCheckRetryMs > 0) {
            schedulePeriodicCheckIn(m_config.periodicCheckRetryMs, /*withJitter=*/false);
        }
        if (manual) {
            // Sin el detalle crudo, un fallo de parseo y un proxy que devuelve HTML se
            // reportan igual que una caida de red, y no hay nada que copiar y mandar.
            showResultDialog(tr("Update Check Failed"),
                             tr("Could not check for updates. Please try again later."),
                             QStringLiteral("httpStatus=%1\nurl=%2\nnetworkError=%3\nbody=%4")
                                 .arg(QString::number(httpStatus), m_config.manifestUrl,
                                      QString::number(static_cast<int>(error)),
                                      QString::fromUtf8(payload.left(2048))),
                             /*warning=*/true);
        }
        return;
    }

    QString manifestFailure;
    const ReleaseInfo info =
        parseManifest(payload, m_config.productId, m_config.releaseRepoSlug, &manifestFailure);
    if (info.version.isEmpty()) {
        if (manual) {
            if (!manifestFailure.isEmpty()) {
                showResultDialog(tr("Update Check Failed"),
                                 tr("The update manifest could not be read safely."),
                                 QStringLiteral("%1\nhttpStatus=%2\nurl=%3\nbody=%4")
                                     .arg(manifestFailure, QString::number(httpStatus),
                                          m_config.manifestUrl,
                                          QString::fromUtf8(payload.left(2048))),
                                 /*warning=*/true);
            } else {
                showResultDialog(tr("Updates"), tr("No installable update was found."),
                                 QStringLiteral("httpStatus=%1\nurl=%2\nbody=%3")
                                     .arg(QString::number(httpStatus), m_config.manifestUrl,
                                          QString::fromUtf8(payload.left(2048))));
            }
        }
        return;
    }

    if (!info.downloadUrl.isValid()) {
        if (manual) {
            showResultDialog(tr("Updates"),
                             tr("Release %1 does not contain an installable asset for %2.")
                                 .arg(info.version, currentPlatformLabel()));
        }
        return;
    }

    if (!isVersionNewer(info.version, QApplication::applicationVersion())) {
        if (manual) {
            showResultDialog(tr("Updates"), tr("You are running the latest version."));
        }
        return;
    }

    // Fail-closed: el manifiesto publica el SHA-256 que GitHub calcula para el asset. Si falta
    // o no tiene 64 hex, no se ofrece una descarga que despues no se podria autenticar.
    if (info.assetDigest.isEmpty()) {
        qWarning().noquote()
            << QStringLiteral("Updater: el release %1 no tiene un SHA-256 valido para %2")
                   .arg(info.version, info.assetName);
        if (manual) {
            showResultDialog(tr("Update Check Failed"),
                             tr("The published update is missing valid integrity information."),
                             QStringLiteral("version=%1\nasset=%2\nrepo=%3")
                                 .arg(info.version, info.assetName, m_config.releaseRepoSlug),
                             /*warning=*/true);
        }
        return;
    }

    // Antes del skip y el snooze: el aviso que no interrumpe sigue valiendo para una version
    // pospuesta. Sin cartel, asi que no abre ningun event loop anidado aca adentro.
    emit updateAvailable(normalizedVersion(info.version));

    // El skip y el snooze valen solo para el chequeo automatico: si el usuario aprieta "Check
    // Now", quiere ver la respuesta aunque haya pospuesto esta version.
    if (!manual) {
        const std::unique_ptr<QSettings> settings = openUpdateSettings(m_config.settingsPath);
        const QString skippedVersion = settings->value("Updates/SkippedVersion").toString();
        const QString snoozeUntilText = settings->value("Updates/SnoozeUntil").toString();

        if (normalizedVersion(skippedVersion) == normalizedVersion(info.version)) {
            return;
        }

        const QDateTime snoozeUntil = QDateTime::fromString(snoozeUntilText, Qt::ISODate);
        if (snoozeUntil.isValid() && QDateTime::currentDateTime() < snoozeUntil) {
            return;
        }

        if (!m_sessionDismissedVersion.isEmpty()
            && m_sessionDismissedVersion == normalizedVersion(info.version)) {
            return;
        }
    }

    if (m_promptVisible) {
        return;
    }

    waitingForNotes = true;
    promptWithNotes(info.version, info.downloadUrl, info.assetName, info.assetDigest, payload);
}

void LgaUpdateService::promptWithNotes(const QString &version,
                                       const QUrl &downloadUrl,
                                       const QString &assetName,
                                       const QString &sha256Digest,
                                       const QByteArray &manifestPayload)
{
    const LgaWhatsNew::AssetRef notesAsset =
        LgaWhatsNew::assetFromManifest(manifestPayload, m_config.releaseRepoSlug);
    if (m_config.whatsNewWaitMs <= 0 || !notesAsset.isValid()) {
        endCheckPhase();
        promptForUpdate(version, downloadUrl, assetName, sha256Digest, QString());
        return;
    }

    // Las notas NUNCA demoran el update mas que `whatsNewWaitMs`: lo que llegue primero entre
    // las notas y ese tope muestra el cartel, y lo otro se ignora. La descarga sigue aunque
    // pierda la carrera (con su propio timeout), para dejar la cache lista para la proxima.
    const quint64 generation = ++m_notesGeneration;
    m_notesPending = true;
    const QString installed = normalizedVersion(QApplication::applicationVersion());
    const auto deliver = [this, generation, version, downloadUrl, assetName,
                          sha256Digest](const QString &notesHtml) {
        if (!m_notesPending || generation != m_notesGeneration) {
            return;
        }
        m_notesPending = false;
        endCheckPhase();
        promptForUpdate(version, downloadUrl, assetName, sha256Digest, notesHtml);
    };

    LgaWhatsNewFetcher *fetcher = startNotesFetch(notesAsset, /*allowStaleCache=*/false);
    connect(fetcher, &LgaWhatsNewFetcher::finished, this,
            [deliver, installed, version](const LgaWhatsNewFetchResult &result) {
                QString notesHtml;
                if (result.status == LgaWhatsNewFetchResult::Status::Ok) {
                    const QList<LgaWhatsNew::VersionNotes> notes = LgaWhatsNew::range(
                        result.notes, installed, version, LgaWhatsNew::currentPlatform());
                    notesHtml = notes.isEmpty() ? QString() : LgaWhatsNew::toHtml(notes);
                } else {
                    qWarning().noquote()
                        << QStringLiteral("Updater: el cartel sale sin notas: %1").arg(result.detail);
                }
                // Si el cartel ya salio sin notas por el tope, esto no hace nada: la descarga
                // solo dejo la cache lista.
                deliver(notesHtml);
            });
    QTimer::singleShot(m_config.whatsNewWaitMs, this, [deliver]() { deliver(QString()); });
}

void LgaUpdateService::promptForUpdate(const QString &version,
                                       const QUrl &downloadUrl,
                                       const QString &assetName,
                                       const QString &sha256Digest,
                                       const QString &notesHtml)
{
    if (m_promptVisible) {
        return;
    }

    if (m_config.prepareForUpdatePrompt) {
        m_config.prepareForUpdatePrompt();
    }

    enum class UpdateDialogAction {
        Cancel,
        Update,
        Snooze,
        Skip
    };

    m_promptVisible = true;
    const QList<ConfirmDialog::Button> buttons = {
        {tr("&Update now"), ConfirmDialog::ButtonStyle::Action, Qt::Key_U},
        {tr("&Remind me later"), ConfirmDialog::ButtonStyle::Neutral, Qt::Key_R},
        {tr("&Skip this version"), ConfirmDialog::ButtonStyle::Neutral, Qt::Key_S},
        {tr("&Cancel"), ConfirmDialog::ButtonStyle::Neutral, Qt::Key_C},
    };
    const QString message =
        tr("A new version of %1 is available: %2.<br><br>"
           "You are currently running version %3.<br><br>"
           "%4")
            .arg(m_config.displayName.toHtmlEscaped(),
                 QStringLiteral("<b>%1</b>").arg(version.toHtmlEscaped()),
                 QStringLiteral("<b>%1</b>").arg(QApplication::applicationVersion().toHtmlEscaped()),
                 QStringLiteral("<b>%1</b>").arg(
                     tr("Save your work and close every other %1 window before updating.")
                         .arg(m_config.displayName.toHtmlEscaped())));
    ConfirmDialog::Spec spec;
    spec.iconName = QStringLiteral("download");
    spec.title = tr("Update Available");
    spec.subtitle = message;
    spec.hintHtml = tr("<b>U</b> Update now · <b>R</b> Remind me later · <b>S</b> Skip this version<br>"
                       "<b>Enter</b> / <b>Esc</b> / <b>C</b> Cancel");
    spec.buttons = buttons;
    spec.defaultIndex = 3;
    spec.escapeIndex = 3;
    // Las notas de (instalada, ofrecida] van en la caja con scroll del cartel. Sin notas es el
    // cartel de siempre.
    spec.contentHtml = notesHtml;
    if (!notesHtml.isEmpty()) {
        // Se guarda ANTES de mostrar: lo que cuenta es que las notas se vieron, decida lo que
        // decida. Asi, despues de instalar esta version, no se vuelven a mostrar.
        storeWhatsNewLastSeen(normalizedVersion(version));
    }
    const int choice = ConfirmDialog::ask(parentWindow(), spec);
    m_promptVisible = false;

    UpdateDialogAction action = UpdateDialogAction::Cancel;
    if (choice == 0) {
        action = UpdateDialogAction::Update;
    } else if (choice == 1) {
        action = UpdateDialogAction::Snooze;
    } else if (choice == 2) {
        action = UpdateDialogAction::Skip;
    }

    // Cancel (o la X) es "ahora no": calla esta version hasta reiniciar la app. Sin esto el
    // chequeo periodico la volvia a ofrecer cada pocas horas.
    if (action == UpdateDialogAction::Cancel) {
        m_sessionDismissedVersion = normalizedVersion(version);
        return;
    }

    if (action == UpdateDialogAction::Update) {
        if (m_config.installBlockedReason) {
            const QString reason = m_config.installBlockedReason();
            if (!reason.isEmpty()) {
                showResultDialog(tr("Update Postponed"), reason, QString(),
                                 /*warning=*/true);
                return;
            }
        }
        // Actualizar ahora limpia el snooze y el skip: la decision anterior quedo sin efecto y
        // no tiene que reaparecer si el update falla y se reintenta.
        const std::unique_ptr<QSettings> settings = openUpdateSettings(m_config.settingsPath);
        settings->remove("Updates/SnoozeUntil");
        settings->remove("Updates/SkippedVersion");

        m_pendingVersion = normalizedVersion(version);
        downloadAndRunUpdate(downloadUrl, assetName, sha256Digest);
        return;
    }

    if (action == UpdateDialogAction::Snooze) {
        const std::unique_ptr<QSettings> settings = openUpdateSettings(m_config.settingsPath);
        settings->setValue("Updates/SnoozeUntil",
                           QDateTime::currentDateTime().addDays(kSnoozeDays).toString(Qt::ISODate));
        return;
    }

    if (action == UpdateDialogAction::Skip) {
        const std::unique_ptr<QSettings> settings = openUpdateSettings(m_config.settingsPath);
        settings->setValue("Updates/SkippedVersion", normalizedVersion(version));
    }
}

// ------------------------------------------------------------------ notas ("What's new")

QString LgaUpdateService::whatsNewCacheDir() const
{
    // La carpeta del .ini de la app y no la de cache de la instalacion: son unos KB y van con
    // el resto de la configuracion.
    if (!m_config.settingsPath.isEmpty()) {
        return QFileInfo(m_config.settingsPath).absolutePath();
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

void LgaUpdateService::storeWhatsNewLastSeen(const QString &version)
{
    if (!LgaWhatsNew::isValidVersion(version)) {
        return;
    }
    const std::unique_ptr<QSettings> settings = openUpdateSettings(m_config.settingsPath);
    const QString stored = settings->value(kWhatsNewLastSeenKey).toString().trimmed();
    bool comparable = false;
    const int order = LgaWhatsNew::compareVersions(stored, version, &comparable);
    if (comparable && order >= 0) {
        return;  // se guarda el MAXIMO: nunca se baja
    }
    settings->setValue(kWhatsNewLastSeenKey, normalizedVersion(version));
}

void LgaUpdateService::runDeferredAutomaticCheck()
{
    if (!m_automaticCheckDeferred || m_afterUpdateRunning || m_historyRunning
        || m_whatsNewDialogVisible || m_resultDialogVisible) {
        return;
    }
    m_automaticCheckDeferred = false;
    QTimer::singleShot(0, this, [this]() { checkForUpdates(false); });
}

void LgaUpdateService::requestManifest(
    std::function<void(bool ok, const QByteArray &payload, const QString &detail)> onDone)
{
    if (!m_network) {
        QTimer::singleShot(0, this, [onDone]() {
            onDone(false, QByteArray(), QStringLiteral("sin network manager"));
        });
        return;
    }
    QNetworkRequest request{QUrl(m_config.manifestUrl)};
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("%1/%2").arg(m_config.productId, QApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(m_config.checkTimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::sslErrors, this,
            [](const QList<QSslError> &errors) { logSslErrors(errors); });

    // `setTransferTimeout` es de INACTIVIDAD: un servidor que gotea un byte cada tanto no lo
    // dispara nunca, y quien espera este manifiesto (el historial con su cursor de espera, el
    // "What's new" de despues de instalar) tiene pospuesto el chequeo automatico mientras
    // tanto. Este tope es TOTAL: al vencer aborta el reply, que termina por `finished`.
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, [reply]() { reply->abort(); });
    deadline->start(m_config.checkTimeoutMs);

    connect(reply, &QNetworkReply::finished, this, [reply, deadline, onDone]() {
        deadline->stop();
        const QNetworkReply::NetworkError error = reply->error();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        const QString detail = QStringLiteral("manifiesto httpStatus=%1 networkError=%2 (%3)")
                                   .arg(httpStatus)
                                   .arg(static_cast<int>(error))
                                   .arg(reply->errorString());
        reply->deleteLater();
        onDone(error == QNetworkReply::NoError, payload, detail);
    });
}

LgaWhatsNewFetcher *LgaUpdateService::startNotesFetch(const LgaWhatsNew::AssetRef &asset,
                                                      bool allowStaleCache)
{
    auto *fetcher = new LgaWhatsNewFetcher(m_network, this);
    connect(fetcher, &LgaWhatsNewFetcher::finished, fetcher, &QObject::deleteLater);
    LgaWhatsNewFetcher::Request request;
    request.repoSlug = m_config.releaseRepoSlug;
    request.asset = asset;
    request.cacheDir = whatsNewCacheDir();
    request.timeoutMs = m_config.checkTimeoutMs;
    request.userAgent =
        QStringLiteral("%1/%2").arg(m_config.productId, QApplication::applicationVersion());
    request.allowStaleCache = allowStaleCache;
    fetcher->fetch(request);
    return fetcher;
}

void LgaUpdateService::showWhatsNewDialog(QWidget *parent,
                                          const QString &title,
                                          const QString &message,
                                          const QString &notesHtml)
{
    ConfirmDialog::Spec spec;
    spec.iconName = QStringLiteral("refresh");
    spec.title = title;
    spec.subtitle = message;
    spec.contentHtml = notesHtml;
    spec.hintHtml = tr("<b>Enter</b> / <b>Esc</b> / <b>O</b> OK");
    spec.buttons = {{tr("&OK"), ConfirmDialog::ButtonStyle::Action, Qt::Key_O}};
    spec.defaultIndex = 0;
    spec.escapeIndex = 0;
    // Cuenta como ocupado mientras el modal corre su event loop: ahi adentro el chequeo
    // automatico podria disparar y apilar el "Update Available" encima.
    m_whatsNewDialogVisible = true;
    ConfirmDialog::ask(parent, spec);
    m_whatsNewDialogVisible = false;
}

void LgaUpdateService::finishWhatsNewAfterUpdate()
{
    m_afterUpdateRunning = false;
    runDeferredAutomaticCheck();
}

void LgaUpdateService::runWhatsNewAfterUpdate()
{
    // Un build que no se actualiza (el de desarrollo) comparte el .ini con la app instalada: si
    // anotara la version como vista, la app instalada dejaria de mostrar esas notas. No sale a la
    // red ni toca `Updates/WhatsNewLastSeen`.
    if (m_config.updateBlockedReason && !m_config.updateBlockedReason().isEmpty()) {
        finishWhatsNewAfterUpdate();
        return;
    }

    const QString current = normalizedVersion(QApplication::applicationVersion());
    if (!LgaWhatsNew::isValidVersion(current)) {
        finishWhatsNewAfterUpdate();
        return;
    }

    const std::unique_ptr<QSettings> settings = openUpdateSettings(m_config.settingsPath);
    const QString lastSeen = settings->value(kWhatsNewLastSeenKey).toString().trimmed();
    if (!LgaWhatsNew::isValidVersion(lastSeen)) {
        // Instalacion nueva, o primera version con esta funcion: no hay forma de saber que vio
        // el usuario, asi que no se le muestra nada y se arranca a contar desde aca.
        settings->setValue(kWhatsNewLastSeenKey, current);
        finishWhatsNewAfterUpdate();
        return;
    }
    if (LgaWhatsNew::compareVersions(current, lastSeen) <= 0) {
        // Misma version, o una anterior (downgrade): nada que contar. No se baja el registro.
        finishWhatsNewAfterUpdate();
        return;
    }

    requestManifest([this, current, lastSeen](bool manifestOk, const QByteArray &payload,
                                              const QString &manifestDetail) {
        const LgaWhatsNew::AssetRef asset = manifestOk
            ? LgaWhatsNew::assetFromManifest(payload, m_config.releaseRepoSlug)
            : LgaWhatsNew::AssetRef{};
        LgaWhatsNewFetcher *fetcher = startNotesFetch(asset, /*allowStaleCache=*/false);
        connect(fetcher, &LgaWhatsNewFetcher::finished, this,
                [this, current, lastSeen, manifestOk,
                 manifestDetail](const LgaWhatsNewFetchResult &result) {
                    QList<LgaWhatsNew::VersionNotes> notes;
                    if (result.status == LgaWhatsNewFetchResult::Status::Ok) {
                        notes = LgaWhatsNew::range(result.notes, lastSeen, current,
                                                   LgaWhatsNew::currentPlatform());
                    }
                    if (notes.isEmpty()) {
                        // Solo una respuesta DEFINITIVA cierra el aviso: notas leidas sin nada
                        // para esta plataforma, o un producto que no publica notas. Una falla
                        // (manifiesto o descarga: red caida, timeout, digest malo) no guarda
                        // nada, asi el proximo arranque lo reintenta. Sin manifiesto el fetcher
                        // devuelve NoNotes porque no tiene asset, por eso se mira `manifestOk`.
                        const bool definitive = manifestOk
                            && result.status != LgaWhatsNewFetchResult::Status::Failed;
                        if (definitive) {
                            storeWhatsNewLastSeen(current);
                        } else {
                            qWarning().noquote()
                                << QStringLiteral("Updater: What's new sin notas, se reintenta en "
                                                  "el proximo arranque: %1")
                                       .arg(manifestOk ? result.detail : manifestDetail);
                        }
                        finishWhatsNewAfterUpdate();
                        return;
                    }
                    if (isBusy()) {
                        // Un cartel de update ya esta en pantalla (el usuario apreto "Check now"
                        // en el primer segundo). No se apila otro: el registro no se toca y el
                        // "What's new" sale en el proximo arranque.
                        finishWhatsNewAfterUpdate();
                        return;
                    }
                    // Se guarda ANTES de mostrar: si la app se cierra con el cartel abierto,
                    // no se vuelve a mostrar. "Una vez" es una vez.
                    storeWhatsNewLastSeen(current);
                    // La residente puede haber arrancado oculta en la bandeja: el modal necesita
                    // un owner visible, igual que el cartel de version nueva.
                    if (m_config.prepareForUpdatePrompt) {
                        m_config.prepareForUpdatePrompt();
                    }
                    showWhatsNewDialog(
                        parentWindow(), tr("What's New"),
                        tr("%1 was updated to version %2.")
                            .arg(m_config.displayName.toHtmlEscaped(),
                                 QStringLiteral("<b>%1</b>").arg(current.toHtmlEscaped())),
                        LgaWhatsNew::toHtml(notes));
                    finishWhatsNewAfterUpdate();
                });
    });
}

void LgaUpdateService::showWhatsNewHistory(QWidget *dialogParent)
{
    if (m_historyRunning || m_whatsNewDialogVisible) {
        return;
    }
    m_historyRunning = true;
    // Lo pide un click: sin el cursor de espera, la demora de la red (casi siempre menos de un
    // segundo, como mucho `checkTimeoutMs`) se ve como un boton que no hizo nada.
    QApplication::setOverrideCursor(Qt::BusyCursor);
    const QPointer<QWidget> requestedParent = dialogParent;

    requestManifest([this, requestedParent](bool manifestOk, const QByteArray &payload,
                                            const QString &manifestDetail) {
        const LgaWhatsNew::AssetRef asset = manifestOk
            ? LgaWhatsNew::assetFromManifest(payload, m_config.releaseRepoSlug)
            : LgaWhatsNew::AssetRef{};
        // Sin red el historial sale de la cache aunque no se pueda confirmar que sea la ultima:
        // fue verificada al guardarse, y es mejor que un cartel de error.
        LgaWhatsNewFetcher *fetcher = startNotesFetch(asset, /*allowStaleCache=*/true);
        connect(fetcher, &LgaWhatsNewFetcher::finished, this,
                [this, requestedParent, manifestOk,
                 manifestDetail](const LgaWhatsNewFetchResult &result) {
                    QApplication::restoreOverrideCursor();
                    m_historyRunning = false;
                    QWidget *const parent =
                        requestedParent ? requestedParent.data() : parentWindow();

                    if (result.status == LgaWhatsNewFetchResult::Status::Ok) {
                        const QList<LgaWhatsNew::VersionNotes> notes = LgaWhatsNew::filtered(
                            result.notes, LgaWhatsNew::currentPlatform());
                        if (!notes.isEmpty()) {
                            showWhatsNewDialog(
                                parent, tr("What's New"),
                                tr("Release notes of %1. You are running version %2.")
                                    .arg(m_config.displayName.toHtmlEscaped(),
                                         QStringLiteral("<b>%1</b>").arg(
                                             QApplication::applicationVersion().toHtmlEscaped())),
                                LgaWhatsNew::toHtml(notes));
                        } else {
                            showResultDialog(tr("What's New"),
                                             tr("There are no release notes for %1 yet.")
                                                 .arg(m_config.displayName));
                        }
                    } else if (manifestOk
                               && result.status == LgaWhatsNewFetchResult::Status::NoNotes) {
                        showResultDialog(tr("What's New"),
                                         tr("There are no release notes for %1 yet.")
                                             .arg(m_config.displayName));
                    } else {
                        showResultDialog(tr("What's New"),
                                         tr("The release notes could not be loaded.\n"
                                            "Check your connection and try again."),
                                         manifestOk ? result.detail : manifestDetail);
                    }
                    runDeferredAutomaticCheck();
                });
    });
}

QString LgaUpdateService::resolveUpdateDir(const QString &updateCacheDir)
{
    QString cacheRoot = updateCacheDir.trimmed();
    if (cacheRoot.isEmpty()) {
        // Compatibilidad: un consumidor que no fije la carpeta sigue bajando donde bajaba.
        cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    }
    if (cacheRoot.isEmpty()) {
        cacheRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    if (cacheRoot.isEmpty()) {
        return QString();
    }
    return QDir(cacheRoot).filePath(QStringLiteral("updates"));
}

void LgaUpdateService::sweepUpdateCache(const QString &productId, const QString &updateCacheDir)
{
    const QString ownDir = resolveUpdateDir(updateCacheDir);
    if (!ownDir.isEmpty()) {
        // Carpeta propia, adentro de la instalacion: se vacia entera. Al arrancar no hay
        // ninguna descarga en vuelo, y una descarga a medias no sirve —`QSaveFile` solo deja
        // el archivo final al terminar, asi que no hay nada que reanudar entre sesiones.
        const QFileInfoList entries =
            QDir(ownDir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const QFileInfo &entry : entries) {
            QFile::remove(entry.absoluteFilePath());
        }
    }

    const QString legacyDir = resolveUpdateDir(QString());
    if (legacyDir.isEmpty() || legacyDir == ownDir || productId.trimmed().isEmpty()) {
        return;
    }
    // Ubicacion vieja: es del sistema y la comparten otras cosas, asi que se borra SOLO lo
    // que lleva el nombre de este producto (el instalador y sus scripts helper).
    QDir legacy(legacyDir);
    const QFileInfoList staleEntries =
        legacy.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &entry : staleEntries) {
        if (!entry.fileName().startsWith(productId, Qt::CaseInsensitive)) {
            continue;
        }
        QFile::remove(entry.absoluteFilePath());
    }
    // Si quedo vacia, se saca tambien la carpeta: era nuestra y ya no se usa.
    QDir().rmdir(legacyDir);
}

void LgaUpdateService::downloadAndRunUpdate(const QUrl &downloadUrl,
                                            const QString &assetName,
                                            const QString &sha256Digest)
{
    if (!m_network || m_downloadReply) {
        return;
    }

    const QString fallbackVersion =
        m_pendingVersion.isEmpty() ? QApplication::applicationVersion() : m_pendingVersion;
    m_pendingAssetName = safeInstallerAssetName(assetName, m_config.productId, fallbackVersion);
    m_pendingAssetSha256 = normalizedSha256Digest(sha256Digest);

    // El destino se resuelve ANTES de bajar nada: el asset se escribe al disco a medida que
    // llega, asi que el archivo tiene que existir antes del primer byte.
    const QString updateDir = resolveUpdateDir(m_config.updateCacheDir);
    if (updateDir.isEmpty() || !QDir().mkpath(updateDir)) {
        showResultDialog(tr("Update Failed"),
                         tr("The update cache directory could not be created."), QString(),
                         /*warning=*/true);
        return;
    }
    // El discard va ANTES de fijar el destino: limpia todo el estado de una descarga previa,
    // incluido `m_downloadTargetPath`, asi que hacerlo despues dejaria el path en vacio y el
    // QSaveFile se construiria sin nombre.
    discardPartialDownload();
    m_downloadTargetPath = QDir(updateDir).filePath(m_pendingAssetName);

    // Barrer lo que quedo de updates anteriores. Nada borra el instalador despues de usarlo
    // —ni siquiera en el camino exitoso, que termina pidiendo el cierre de la app— asi que se
    // acumularia un instalador completo POR VERSION PUBLICADA en el cache del usuario. Se
    // barre aca y no al terminar porque en el camino exitoso la app ya no esta viva.
    //
    // Se conserva el asset en curso: puede ser una descarga parcial que se va a reanudar por
    // `Range`, y borrarla obligaria a bajar todo de nuevo.
    const QString currentAssetName = QFileInfo(m_downloadTargetPath).fileName();
    const QFileInfoList staleEntries =
        QDir(updateDir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &stale : staleEntries) {
        if (stale.fileName() == currentAssetName) {
            continue;
        }
        QFile::remove(stale.absoluteFilePath());
    }

    m_downloadFile = new QSaveFile(m_downloadTargetPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        const QString detail = m_downloadFile->errorString();
        discardPartialDownload();
        showResultDialog(tr("Update Failed"), tr("The update installer could not be saved."),
                         detail, /*warning=*/true);
        return;
    }
    m_downloadHash = new QCryptographicHash(QCryptographicHash::Sha256);
    m_downloadUrl = downloadUrl;
    m_downloadWritten = 0;
    m_downloadTotal = -1;
    m_downloadRetries = 0;
    m_downloadSpaceChecked = false;
    m_downloadUserCancelled = false;

    m_progressDialog = new QProgressDialog(
        tr("Downloading %1 %2...").arg(m_config.displayName, fallbackVersion), tr("Cancel"), 0, 0,
        parentWindow());
    m_progressDialog->setObjectName(QStringLiteral("updateDownloadProgressDialog"));
    m_progressDialog->setWindowTitle(tr("Downloading Update"));
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setAutoClose(false);
    m_progressDialog->setAutoReset(false);
    m_progressDialog->show();

    connect(m_progressDialog, &QProgressDialog::canceled, this, [this]() {
        m_downloadUserCancelled = true;
        if (m_downloadReply) {
            m_downloadReply->abort();
        }
    });

    startDownloadRequest(0);
}

void LgaUpdateService::startDownloadRequest(qint64 resumeFrom)
{
    if (!m_network || m_downloadReply || !m_downloadFile) {
        return;
    }

    QNetworkRequest request{m_downloadUrl};
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("%1/%2").arg(m_config.productId, QApplication::applicationVersion()));
    // Timeout de INACTIVIDAD, no total: una descarga lenta de 40 minutos no se corta mientras
    // fluyan bytes.
    request.setTransferTimeout(m_config.downloadTimeoutMs);
    if (resumeFrom > 0) {
        request.setRawHeader("Range", QStringLiteral("bytes=%1-").arg(resumeFrom).toLatin1());
    }

    // Estado POR PETICION: se reinicia en cada intento, incluidos los reintentos.
    m_downloadResponseChecked = false;
    m_downloadResumeOffset = resumeFrom;

    m_downloadReply = m_network->get(request);

    connect(m_downloadReply, &QNetworkReply::readyRead, this,
            &LgaUpdateService::onDownloadReadyRead);
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 bytesReceived, qint64 bytesTotal) {
                // `bytesReceived` es de ESTA peticion; con resume hay que sumarle lo que ya
                // estaba escrito, o la barra arrancaria de cero en cada reintento.
                const qint64 base = m_downloadWritten - bytesReceived;
                const qint64 total = (bytesTotal > 0) ? (bytesTotal + qMax<qint64>(base, 0)) : -1;
                if (m_downloadFailure != DownloadFailure::None) {
                    return; // ya se decidio abortar: no se resucita estado
                }
                if (total > 0) {
                    m_downloadTotal = total;
                    if (!m_downloadSpaceChecked) {
                        m_downloadSpaceChecked = true;
                        if (!ensureFreeSpaceForDownload(total)) {
                            abortDownloadDeferred();
                            return;
                        }
                    }
                    if (m_progressDialog) {
                        m_progressDialog->setMaximum(static_cast<int>(total / 1024));
                        m_progressDialog->setValue(static_cast<int>(m_downloadWritten / 1024));
                    }
                }
            });
    connect(m_downloadReply, &QNetworkReply::finished, this,
            &LgaUpdateService::onDownloadFinished);
    connect(m_downloadReply, &QNetworkReply::sslErrors, this,
            [](const QList<QSslError> &errors) { logSslErrors(errors); });
}

bool LgaUpdateService::validateDownloadResponse()
{
    if (!m_downloadReply) {
        return false;
    }

    const int status = m_downloadReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (m_downloadResumeOffset > 0) {
        // Se pidio un rango. Un servidor que lo IGNORA responde 200 con el cuerpo completo: si
        // se siguiera appendeando, el archivo quedaria con los primeros bytes duplicados y el
        // hash calculado sobre basura. Sin digest en el release eso se commitea y se lanza como
        // instalador. Por eso, ante un 200 se descarta lo bajado y se empieza de nuevo.
        if (status == 200) {
            if (!m_downloadFile->resize(0) || !m_downloadFile->seek(0)) {
                m_downloadFailure = DownloadFailure::Resume;
                m_downloadFailureDetail =
                    QStringLiteral("El servidor ignoro el Range (200) y no se pudo reiniciar el "
                                   "archivo parcial.\n%1")
                        .arg(m_downloadFile->errorString());
                return false;
            }
            if (m_downloadHash) {
                m_downloadHash->reset();
            }
            m_downloadWritten = 0;
            m_downloadResumeOffset = 0;
            // Se invalida el total: quedo calculado sumando el offset viejo y, hasta la proxima
            // emision de progreso, haria fallar el chequeo de completitud.
            m_downloadTotal = -1;
            return true;
        }

        if (status != 206) {
            m_downloadFailure = DownloadFailure::Http;
            m_downloadFailureDetail = QStringLiteral("httpStatus=%1 al reanudar desde el byte %2")
                                          .arg(QString::number(status),
                                               QString::number(m_downloadResumeOffset));
            return false;
        }

        // Con 206, el rango que manda el servidor tiene que arrancar EXACTAMENTE donde
        // quedamos. Un servidor que clampea el offset produce el mismo archivo corrupto.
        const QByteArray contentRange = m_downloadReply->rawHeader("Content-Range");
        const qsizetype spaceIndex = contentRange.indexOf(' ');
        const qsizetype dashIndex = contentRange.indexOf('-');
        bool parsedOk = false;
        qint64 rangeStart = -1;
        if (spaceIndex >= 0 && dashIndex > spaceIndex) {
            rangeStart = contentRange.mid(spaceIndex + 1, dashIndex - spaceIndex - 1)
                             .trimmed()
                             .toLongLong(&parsedOk);
        }
        if (!parsedOk || rangeStart != m_downloadResumeOffset) {
            m_downloadFailure = DownloadFailure::Resume;
            m_downloadFailureDetail =
                QStringLiteral("Content-Range inesperado al reanudar.\nesperado=%1\nrecibido=%2")
                    .arg(QString::number(m_downloadResumeOffset),
                         QString::fromLatin1(contentRange));
            return false;
        }
        return true;
    }

    // Peticion normal: cualquier cosa que no sea 200 es una pagina de error, y escribirla al
    // archivo la haria pasar por instalador.
    if (status != 200) {
        m_downloadFailure = DownloadFailure::Http;
        m_downloadFailureDetail = QStringLiteral("httpStatus=%1").arg(QString::number(status));
        return false;
    }
    return true;
}

void LgaUpdateService::onDownloadReadyRead()
{
    if (!m_downloadReply || !m_downloadFile) {
        return;
    }
    if (m_downloadFailure != DownloadFailure::None) {
        return; // ya se decidio abortar; no se escribe nada mas
    }

    if (!m_downloadResponseChecked) {
        m_downloadResponseChecked = true;
        if (!validateDownloadResponse()) {
            abortDownloadDeferred();
            return;
        }
    }

    // Se drena el reply en cada senal. Si no se lo drena, Qt acumula TODO el cuerpo en su
    // buffer interno y la memoria crece con el tamano del asset.
    const QByteArray chunk = m_downloadReply->readAll();
    if (chunk.isEmpty()) {
        return;
    }

    if (m_downloadFile->write(chunk) != chunk.size()) {
        // Sin cartel: se anota el motivo y decide `onDownloadFinished`. Un modal aca abre un
        // event loop anidado en medio de la entrega de datos.
        m_downloadFailure = DownloadFailure::Write;
        m_downloadFailureDetail = QStringLiteral("write fallo tras %1 bytes\n%2")
                                      .arg(QString::number(m_downloadWritten),
                                           m_downloadFile->errorString());
        abortDownloadDeferred();
        return;
    }

    if (m_downloadHash) {
        m_downloadHash->addData(chunk);
    }
    m_downloadWritten += chunk.size();
}

bool LgaUpdateService::ensureFreeSpaceForDownload(qint64 totalBytes)
{
    if (totalBytes <= 0 || m_downloadTargetPath.isEmpty()) {
        return true;
    }

    const QStorageInfo storage(QFileInfo(m_downloadTargetPath).absolutePath());
    if (!storage.isValid() || !storage.isReady()) {
        return true; // sin dato confiable no se bloquea la descarga
    }

    // Margen del 15%: ademas del zip hay que poder extraerlo y dejar respirar al disco.
    const qint64 needed = totalBytes + (totalBytes / 100 * 15);
    if (storage.bytesAvailable() >= needed) {
        return true;
    }

    const auto toMb = [](qint64 bytes) { return QString::number(bytes / (1024 * 1024)); };
    m_downloadFailure = DownloadFailure::DiskSpace;
    m_downloadFailureDetail =
        QStringLiteral("volumen=%1\nnecesario=%2 MB\ndisponible=%3 MB")
            .arg(storage.rootPath(), toMb(needed), toMb(storage.bytesAvailable()));
    return false;
}

void LgaUpdateService::abortDownloadDeferred()
{
    // `QNetworkReply::abort()` emite `finished()` SINCRONICAMENTE. Llamarlo desde un handler de
    // red hace correr `onDownloadFinished` —y su cartel modal— anidado dentro de la entrega de
    // datos, con el reply todavia en el stack: al desapilar, Qt sigue emitiendo senales sobre
    // un estado ya limpiado. Diferirlo al event loop corta ese anidamiento.
    QTimer::singleShot(0, this, [this]() {
        if (m_downloadReply) {
            m_downloadReply->abort();
        }
    });
}

void LgaUpdateService::showResultDialog(const QString &title,
                                        const QString &message,
                                        const QString &rawDetail,
                                        bool warning)
{
    Q_UNUSED(warning)
    // Un cartel de resultado cierra la fase de chequeo ANTES de abrirse (ver `checkFinished`).
    endCheckPhase();
    // El flag cuenta como "ocupado" mientras el modal corre su event loop anidado: ahi adentro
    // el timer del chequeo automatico sigue disparando y podria apilar otro cartel.
    //
    // Se GUARDA y se restaura en vez de escribir `false` al salir: dentro del event loop de un
    // cartel se puede abrir otro (la descarga en curso termina mientras el usuario todavia no
    // cerro el aviso de "ya estas al dia"), y el de adentro, al cerrarse, apagaria el flag con
    // el de afuera todavia abierto. Ahi `isBusy()` mentiria y el chequeo automatico podria
    // apilar un tercer cartel encima.
    const bool wasResultDialogVisible = m_resultDialogVisible;
    m_resultDialogVisible = true;
    ConfirmDialog::Spec spec;
    spec.iconName = QStringLiteral("refresh");
    spec.title = title;
    spec.subtitle = message.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    spec.details = rawDetail;
    spec.hintHtml = tr("<b>Enter</b> / <b>Esc</b> / <b>O</b> OK");
    spec.buttons = {{tr("&OK"), ConfirmDialog::ButtonStyle::Action, Qt::Key_O}};
    spec.defaultIndex = 0;
    spec.escapeIndex = 0;
    ConfirmDialog::ask(parentWindow(), spec);
    m_resultDialogVisible = wasResultDialogVisible;
    // Si el chequeo automatico se pospuso mientras este cartel estaba abierto, corre ahora.
    runDeferredAutomaticCheck();
}

void LgaUpdateService::closeProgressDialog()
{
    if (!m_progressDialog) {
        return;
    }
    // `QProgressDialog::closeEvent()` emite `canceled()`. Si se cierra con las conexiones
    // vivas, ese `canceled()` marca como cancelada por el usuario a una descarga que acaba de
    // terminar bien, y el instalador se descarta sin instalar ni avisar nada.
    m_progressDialog->disconnect(this);
    m_progressDialog->close();
    m_progressDialog->deleteLater();
    m_progressDialog = nullptr;
}

void LgaUpdateService::discardPartialDownload()
{
    if (m_downloadFile) {
        // cancelWriting() borra el temporal: un parcial no se deja en disco haciendose pasar por
        // un instalador valido.
        m_downloadFile->cancelWriting();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }
    delete m_downloadHash;
    m_downloadHash = nullptr;
    m_downloadWritten = 0;
    m_downloadTotal = -1;
    m_downloadRetries = 0;
    m_downloadSpaceChecked = false;
    m_downloadUserCancelled = false;
    m_downloadResponseChecked = false;
    m_downloadResumeOffset = 0;
    m_downloadFailure = DownloadFailure::None;
    m_downloadFailureDetail.clear();
    m_downloadTargetPath.clear();
}

void LgaUpdateService::onDownloadFinished()
{
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;

    if (!reply) {
        return;
    }

    // OJO: el dialogo de progreso NO se cierra aca. Cerrarlo emite `canceled()`, y si eso
    // ocurre antes de leer el flag de cancelacion, una descarga exitosa queda marcada como
    // cancelada por el usuario. Se cierra con closeProgressDialog(), y solo en los desenlaces
    // terminales: en un reintento tiene que seguir abierto.
    const QNetworkReply::NetworkError error = reply->error();

    // Lo que quedaba en el buffer: `finished` puede llegar con bytes sin drenar. Un fallo de
    // escritura aca NO se traga: se anota como los demas.
    if (m_downloadFile && m_downloadFailure == DownloadFailure::None
        && reply->bytesAvailable() > 0) {
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty()) {
            if (m_downloadFile->write(tail) != tail.size()) {
                m_downloadFailure = DownloadFailure::Write;
                m_downloadFailureDetail =
                    QStringLiteral("write del tramo final fallo tras %1 bytes\n%2")
                        .arg(QString::number(m_downloadWritten), m_downloadFile->errorString());
            } else {
                if (m_downloadHash) {
                    m_downloadHash->addData(tail);
                }
                m_downloadWritten += tail.size();
            }
        }
    }
    const QString replyErrorString = reply->errorString();
    reply->deleteLater();

    // Cancelar es una decision del usuario, no un fallo: no lleva cartel, pero el parcial se
    // tira igual.
    if (m_downloadUserCancelled) {
        closeProgressDialog();
        discardPartialDownload();
        return;
    }

    if (m_downloadFailure == DownloadFailure::None && error != QNetworkReply::NoError) {
        m_downloadFailure = DownloadFailure::Network;
        m_downloadFailureDetail = QStringLiteral("networkError=%1 (%2)\nurl=%3\nbytesWritten=%4")
                                      .arg(QString::number(static_cast<int>(error)),
                                           replyErrorString, m_downloadUrl.toString(),
                                           QString::number(m_downloadWritten));
    }

    // Un corte a mitad de camino se reanuda desde donde quedo en vez de empezar de cero: con
    // assets de cientos de MB, reiniciar por un corte al 90% es tirar todo. Se discrimina por
    // el FLAG de cancelacion del usuario, no por el codigo de error: el transfer timeout de Qt
    // tambien produce OperationCanceledError, y es justo el caso que mas conviene reanudar.
    //
    // NO se reintenta si ya se bajo todo. Un cierre sucio de conexion justo despues del ultimo
    // byte pedia `Range: bytes=<tamano total>-`, y el 416 que devuelve el servidor terminaba
    // tirando un archivo completo y valido.
    const bool alreadyComplete = m_downloadTotal > 0 && m_downloadWritten >= m_downloadTotal;

    // Si llego el archivo entero, un cierre sucio de la conexion NO es un fallo: el asset esta
    // completo y el SHA-256 lo va a confirmar. Tratarlo como error tiraba una descarga buena.
    if (alreadyComplete && m_downloadFailure == DownloadFailure::Network) {
        m_downloadFailure = DownloadFailure::None;
        m_downloadFailureDetail.clear();
    }

    const bool retriable = m_downloadFailure == DownloadFailure::Network && m_downloadWritten > 0
        && !alreadyComplete && m_downloadRetries < kMaxDownloadRetries;
    if (retriable) {
        ++m_downloadRetries;
        m_downloadFailure = DownloadFailure::None;
        m_downloadFailureDetail.clear();
        // El dialogo de progreso NO se cierra: la descarga sigue, y sin el el usuario se
        // quedaria sin barra y sin boton para cancelar.
        startDownloadRequest(m_downloadWritten);
        return;
    }

    // Descarga completa pero mas corta de lo anunciado: hay que detectarlo antes de commitear.
    if (m_downloadFailure == DownloadFailure::None && m_downloadTotal > 0
        && m_downloadWritten != m_downloadTotal) {
        m_downloadFailure = DownloadFailure::Network;
        m_downloadFailureDetail = QStringLiteral("descarga incompleta: %1 de %2 bytes")
                                      .arg(QString::number(m_downloadWritten),
                                           QString::number(m_downloadTotal));
    }

    if (m_downloadFailure == DownloadFailure::None
        && (!m_downloadFile || m_downloadWritten <= 0)) {
        m_downloadFailure = DownloadFailure::Network;
        m_downloadFailureDetail = QStringLiteral("no se recibio ningun byte");
    }

    if (m_downloadFailure == DownloadFailure::None && !m_pendingAssetSha256.isEmpty()
        && m_downloadHash) {
        const QString downloadedDigest = QString::fromLatin1(m_downloadHash->result().toHex());
        if (downloadedDigest.compare(m_pendingAssetSha256, Qt::CaseInsensitive) != 0) {
            m_downloadFailure = DownloadFailure::Integrity;
            m_downloadFailureDetail = QStringLiteral("esperado=%1\nobtenido=%2\nbytes=%3")
                                          .arg(m_pendingAssetSha256, downloadedDigest,
                                               QString::number(m_downloadWritten));
        }
    }

    if (m_downloadFailure == DownloadFailure::None && !m_downloadFile->commit()) {
        m_downloadFailure = DownloadFailure::Write;
        m_downloadFailureDetail =
            QStringLiteral("commit fallo\n%1").arg(m_downloadFile->errorString());
    }

    closeProgressDialog();

    // UN solo cartel por fallo, y siempre con el detalle crudo detras del boton Details.
    if (m_downloadFailure != DownloadFailure::None) {
        QString message;
        switch (m_downloadFailure) {
        case DownloadFailure::DiskSpace:
            message = tr("There is not enough free disk space to download the update.");
            break;
        case DownloadFailure::Write:
            message = tr("The update installer could not be saved to disk.");
            break;
        case DownloadFailure::Integrity:
            message = tr("The downloaded update failed integrity verification.");
            break;
        case DownloadFailure::Http:
            message = tr("The server rejected the update download.");
            break;
        case DownloadFailure::Resume:
            message = tr("The interrupted download could not be resumed safely.");
            break;
        case DownloadFailure::Network:
        case DownloadFailure::None:
            message = tr("The update installer could not be downloaded.");
            break;
        }
        const QString detail = m_downloadFailureDetail;
        discardPartialDownload();
        showResultDialog(tr("Update Failed"), message, detail, /*warning=*/true);
        return;
    }

    const QString installerPath = m_downloadTargetPath;
    delete m_downloadFile;
    m_downloadFile = nullptr;
    delete m_downloadHash;
    m_downloadHash = nullptr;
    m_downloadWritten = 0;
    m_downloadTotal = -1;
    m_downloadRetries = 0;
    m_downloadSpaceChecked = false;
    m_downloadResponseChecked = false;
    m_downloadResumeOffset = 0;

    if (m_config.installBlockedReason) {
        const QString reason = m_config.installBlockedReason();
        if (!reason.isEmpty()) {
            QFile::remove(installerPath);
            m_downloadTargetPath.clear();
            showResultDialog(tr("Update Postponed"), reason, QString(),
                             /*warning=*/true);
            return;
        }
    }

    QString launchError;
    if (!launchInstallerWithHelper(installerPath, &launchError)) {
        if (launchError.isEmpty()) {
            launchError = tr("The update installer could not be launched.");
        }
        showResultDialog(tr("Update Failed"), launchError, QString(), /*warning=*/true);
        return;
    }

    if (m_config.quitApplication) {
        m_config.quitApplication();
    } else {
        QApplication::quit();
    }
}

bool LgaUpdateService::launchInstallerWithHelper(const QString &installerPath,
                                                 QString *errorMessage)
{
    auto setError = [errorMessage](const QString &message) {
        if (errorMessage) {
            *errorMessage = message;
        }
    };

#ifdef Q_OS_WIN
    const QFileInfo installerInfo(installerPath);
    const QString helperPath = QDir(installerInfo.absolutePath())
                                   .filePath(QStringLiteral("%1_Update_Helper_%2.ps1")
                                                 .arg(m_config.productId)
                                                 .arg(QDateTime::currentMSecsSinceEpoch()));

    const QString appDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const QString executableName = m_config.windowsExecutableName.isEmpty()
        ? QFileInfo(QCoreApplication::applicationFilePath()).fileName()
        : m_config.windowsExecutableName;

    // El script se parametriza por replace y NO por arg(): trae llaves y comodines que conviene
    // no pasar por el formateo de QString.
    const QString helperTemplate = QStringLiteral(
R"(param([string]$InstallerPath, [string]$AppDir)

$ErrorActionPreference = 'SilentlyContinue'

# El helper se borra a si mismo antes de cualquier salida. Se nombra con un timestamp unico,
# asi que sin esto quedaria UNO POR INTENTO de update en el directorio de cache —incluidos los
# que abortan— acumulandose para siempre. PowerShell lee el script entero a memoria antes de
# ejecutarlo, asi que borrarse en vuelo es seguro.
$script:helperSelfPath = $MyInvocation.MyCommand.Path
function Remove-Self {
    if ($script:helperSelfPath) {
        Remove-Item -LiteralPath $script:helperSelfPath -Force -ErrorAction SilentlyContinue
    }
}

if (-not $InstallerPath -or -not (Test-Path -LiteralPath $InstallerPath)) {
    Remove-Self
    exit 1
}

$sleepMs = 500
$appWaitLimitMs = 15000

function Normalize-Path([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ''
    }
    try {
        return [System.IO.Path]::GetFullPath($Path).TrimEnd('\').ToLowerInvariant()
    } catch {
        return $Path.TrimEnd('\').ToLowerInvariant()
    }
}

$appDirNorm = Normalize-Path $AppDir

function Test-InAppDir([string]$Path) {
    $norm = Normalize-Path $Path
    if (-not $appDirNorm -or -not $norm) {
        return $false
    }
    return ($norm -eq $appDirNorm) -or $norm.StartsWith($appDirNorm + '\')
}

# Filtrar por directorio de instalacion evita cerrar OTRA copia de la app que el usuario
# tenga abierto desde otra carpeta.
function Get-AppProcesses {
    Get-CimInstance Win32_Process |
        Where-Object {
            ($_.Name -ieq '__EXECUTABLE_NAME__') -and
            (Test-InAppDir $_.ExecutablePath)
        }
}

function Wait-UntilGone([scriptblock]$Query, [int]$TimeoutMs) {
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((& $Query | Select-Object -First 1) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds $sleepMs
    }
    return -not (& $Query | Select-Object -First 1)
}

if (-not (Wait-UntilGone { Get-AppProcesses } $appWaitLimitMs)) {
    # Nunca forzar el cierre: puede ser una instancia secundaria con una captura sin guardar.
    # El usuario conserva su trabajo y puede reintentar despues de cerrar todas las ventanas.
    try {
        $shell = New-Object -ComObject WScript.Shell
        $shell.Popup('The update was postponed because another __DISPLAY_NAME__ window is still open. Save your work, close every __DISPLAY_NAME__ window, and try again.', 0, '__DISPLAY_NAME__ Update', 48) | Out-Null
    } catch {}
    Remove-Self
    exit 3
}

for ($i = 0; $i -lt 20; $i++) {
    try {
        # Mantener exactamente la carpeta de la instalacion que solicito el update.
        $dirArgument = '/DIR="' + $AppDir + '"'
        Start-Process -FilePath $InstallerPath -ArgumentList $dirArgument
        Remove-Self
        exit 0
    } catch {
        Start-Sleep -Milliseconds $sleepMs
    }
}

Remove-Self
exit 2
)");

    QString helperScript = helperTemplate;
    helperScript.replace(QStringLiteral("__EXECUTABLE_NAME__"), executableName);
    helperScript.replace(QStringLiteral("__DISPLAY_NAME__"), QString(m_config.displayName).replace('\'', "''"));

    QSaveFile helperFile(helperPath);
    const QByteArray helperBytes = helperScript.toUtf8();
    if (!helperFile.open(QIODevice::WriteOnly)
        || helperFile.write(helperBytes) != helperBytes.size() || !helperFile.commit()) {
        setError(tr("Could not prepare update helper script."));
        return false;
    }

    QStringList arguments;
    arguments << "-NoProfile"
              << "-ExecutionPolicy"
              << "Bypass"
              << "-WindowStyle"
              << "Hidden"
              << "-File" << helperPath << installerPath << appDir;
    if (!QProcess::startDetached("powershell", arguments)) {
        setError(tr("Could not launch update helper process."));
        return false;
    }

    return true;
#elif defined(Q_OS_MAC)
    // El destino es el bundle que esta corriendo, sea cual sea su nombre. NO se compara contra
    // `<productId>.app`: el nombre del bundle y el del asset no son necesariamente el mismo
    // contrato. `productId` identifica SOLO al asset del release.
    QString targetAppPath = currentMacAppBundlePath();
    if (targetAppPath.isEmpty()) {
        targetAppPath = QFileInfo(QDir(QCoreApplication::applicationDirPath())
                                      .filePath(QStringLiteral("../..")))
                            .absoluteFilePath();
    }
    if (targetAppPath.isEmpty()
        || !targetAppPath.endsWith(QStringLiteral(".app"), Qt::CaseInsensitive)) {
        setError(tr("Could not resolve the %1 app bundle for update.").arg(m_config.displayName));
        return false;
    }
    const QString bundleName = QFileInfo(targetAppPath).fileName();

    const QFileInfo targetParentInfo(QFileInfo(targetAppPath).absolutePath());
    if (!targetParentInfo.exists() || !targetParentInfo.isWritable()) {
        setError(
            tr("%1 could not update automatically because this location is not writable:\n%2\n\n"
               "Please install the new macOS release manually and run:\n"
               "sudo xattr -cr /Applications/%3")
                .arg(m_config.displayName, targetAppPath, bundleName));
        return false;
    }

    const QString executableName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    const QFileInfo installerInfo(installerPath);
    const QString helperPath = QDir(installerInfo.absolutePath())
                                   .filePath(QStringLiteral("%1_Update_Helper_%2.sh")
                                                 .arg(m_config.productId)
                                                 .arg(QDateTime::currentMSecsSinceEpoch()));

    const QString helperTemplate = QStringLiteral(
R"MACHELPER(#!/bin/bash
set -euo pipefail

ARCHIVE_PATH="$1"
TARGET_APP_PATH="$2"
EXECUTABLE_NAME="$3"
HELPER_PATH="$0"
TARGET_EXEC="$TARGET_APP_PATH/Contents/MacOS/$EXECUTABLE_NAME"
TARGET_PARENT_DIR="$(dirname "$TARGET_APP_PATH")"
UPDATE_LOG="$HOME/Library/Logs/LGA/__PRODUCT_ID__/Update.log"
WORK_DIR=""
BACKUP_PATH=""
TARGET_SWAPPED=false
INSTALL_COMPLETE=false
RELAUNCH_ALLOWED=true

if [ ! -f "$ARCHIVE_PATH" ] || [ -z "$TARGET_APP_PATH" ] || [ -z "$EXECUTABLE_NAME" ]; then
    exit 1
fi

cleanup() {
    EXIT_STATUS=$?
    trap - EXIT
    set +e
    if [ "$INSTALL_COMPLETE" != true ] && [ "$EXIT_STATUS" -eq 0 ]; then
        EXIT_STATUS=1
    fi
    if [ -n "$WORK_DIR" ]; then
        rm -rf "$WORK_DIR"
    fi
    if [ "$INSTALL_COMPLETE" != true ] && [ "$TARGET_SWAPPED" = true ]; then
        rm -rf "$TARGET_APP_PATH"
        if [ -n "$BACKUP_PATH" ] && [ -d "$BACKUP_PATH" ]; then
            mv "$BACKUP_PATH" "$TARGET_APP_PATH" || true
        fi
    fi
    if [ "$INSTALL_COMPLETE" = true ]; then
        [ -n "$BACKUP_PATH" ] && rm -rf "$BACKUP_PATH"
        printf '[%s] Actualizacion completada correctamente.\n' "$(date '+%Y-%m-%d %H:%M:%S')"
    else
        printf '[%s] La actualizacion fallo (estado=%s). Se conserva o restaura la app valida.\n' \
            "$(date '+%Y-%m-%d %H:%M:%S')" "$EXIT_STATUS"
        if [ "$RELAUNCH_ALLOWED" = true ] && [ -x "$TARGET_EXEC" ]; then
            "$TARGET_EXEC" >/dev/null 2>&1 &
        fi
        /usr/bin/osascript -e 'display notification "The update could not finish. __DISPLAY_NAME__ was restored; details were saved in the update log." with title "__DISPLAY_NAME__ Update"' >/dev/null 2>&1 || true
    fi
    rm -f "$HELPER_PATH"
    exit "$EXIT_STATUS"
}
trap cleanup EXIT

mkdir -p "$(dirname "$UPDATE_LOG")" >/dev/null 2>&1 || true
touch "$UPDATE_LOG" >/dev/null 2>&1 || true
if [ -w "$UPDATE_LOG" ]; then
    exec >> "$UPDATE_LOG" 2>&1
fi
printf '[%s] Iniciando actualizacion. archivo=%s destino=%s\n' \
    "$(date '+%Y-%m-%d %H:%M:%S')" "$ARCHIVE_PATH" "$TARGET_APP_PATH"

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/__PRODUCT_ID___Update_XXXXXX")"
/usr/bin/ditto -x -k "$ARCHIVE_PATH" "$WORK_DIR"
# El .app se busca por glob y no por nombre: el nombre del asset no tiene por que predecir el
# del bundle.
NEW_APP_PATH="$(/usr/bin/find "$WORK_DIR" -maxdepth 1 -name '*.app' -print -quit)"
if [ -z "$NEW_APP_PATH" ] || [ ! -d "$NEW_APP_PATH" ] || [ ! -x "$NEW_APP_PATH/Contents/MacOS/$EXECUTABLE_NAME" ]; then
    exit 1
fi
/usr/bin/codesign --verify --deep --strict "$NEW_APP_PATH"
if [ ! -d "$TARGET_PARENT_DIR" ] || [ ! -w "$TARGET_PARENT_DIR" ]; then
    exit 1
fi

# Filtrar por path absoluto evita cerrar otros builds de la misma app.
find_target_pids() {
    CANDIDATE_PIDS="$(/usr/bin/pgrep -f "$EXECUTABLE_NAME" 2>/dev/null || true)"
    for candidate_pid in $CANDIDATE_PIDS; do
        [ "$candidate_pid" = "$$" ] && continue
        candidate_command="$(/bin/ps -p "$candidate_pid" -o command= 2>/dev/null || true)"
        case "$candidate_command" in
            "$TARGET_EXEC"|"$TARGET_EXEC "*) printf '%s\n' "$candidate_pid" ;;
        esac
    done
    return 0
}

TARGET_PIDS="$(find_target_pids)"
for ((i = 0; i < 60; i++)); do
    [ -z "$TARGET_PIDS" ] && break
    sleep 0.5
    TARGET_PIDS="$(find_target_pids)"
done
if [ -n "$TARGET_PIDS" ]; then
    # Nunca mandar TERM/KILL ni abrir otra instancia cuando una de este bundle sigue viva.
    RELAUNCH_ALLOWED=false
    exit 3
fi

BACKUP_PATH="${TARGET_APP_PATH}.backup.$(date +%s).$$"
if [ -e "$TARGET_APP_PATH" ]; then
    mv "$TARGET_APP_PATH" "$BACKUP_PATH"
fi
TARGET_SWAPPED=true
/usr/bin/ditto "$NEW_APP_PATH" "$TARGET_APP_PATH"
if [ ! -x "$TARGET_EXEC" ]; then
    exit 1
fi
/usr/bin/codesign --verify --deep --strict "$TARGET_APP_PATH"
xattr -dr com.apple.quarantine "$TARGET_APP_PATH" >/dev/null 2>&1 || true

"$TARGET_EXEC" >/dev/null 2>&1 &
INSTALL_COMPLETE=true
)MACHELPER");

    QString helperScript = helperTemplate;
    helperScript.replace(QStringLiteral("__PRODUCT_ID__"), m_config.productId);
    helperScript.replace(QStringLiteral("__DISPLAY_NAME__"), m_config.displayName);

    QSaveFile helperFile(helperPath);
    const QByteArray helperBytes = helperScript.toUtf8();
    if (!helperFile.open(QIODevice::WriteOnly)
        || helperFile.write(helperBytes) != helperBytes.size() || !helperFile.commit()) {
        setError(tr("Could not prepare macOS update helper script."));
        return false;
    }

    QStringList arguments;
    arguments << helperPath << installerPath << targetAppPath << executableName;
    if (!QProcess::startDetached("/bin/bash", arguments)) {
        setError(tr("Could not launch macOS update helper process."));
        return false;
    }

    return true;
#else
    if (!QProcess::startDetached(installerPath, QStringList())) {
        setError(tr("Could not launch installer process."));
        return false;
    }
    return true;
#endif
}

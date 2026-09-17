#include "videodownloader/mainwindow.h"
#include "videodownloader/colorutils.h"
#include "videodownloader/LgaRegistry.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QIcon>
#include <QFontDatabase>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>

namespace {

// Devuelve <AppData>/LGA/<appFolder>, con la MISMA logica de plataforma que
// MainWindow::getConfigPath(). No crea el directorio.
QString appDataDirFor(const QString &appFolder)
{
#ifdef Q_OS_WIN
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    base = base.replace("/VideoDownloader", "").replace("\\VideoDownloader", "");
    return base + "/" + appFolder;
#elif defined(Q_OS_MAC)
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    base = base.replace("/VideoDownloader", "");
    return base + "/" + appFolder;
#else
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
           + "/LGA/" + appFolder;
#endif
}

// Migracion one-time del AppData: <AppData>/LGA/VimeoDownloader -> .../VideoDownloader
//
// La app se llamaba VimeoDownloader; el rename cambia la carpeta de settings.
// Sin esto, cada usuario abre la version nueva sin sus credenciales de Vimeo ni
// su carpeta de descargas configurada.
//
// El centinela es el ARCHIVO `config.ini`, NO la existencia del directorio
// destino: `MainWindow::getConfigPath()` hace `mkpath` cada vez que se llama,
// asi que el directorio puede aparecer sin que la migracion haya corrido. Con
// una guarda por directorio, cualquier arranque que abortara la migracion
// dejaba el destino creado y NO se reintentaba nunca mas, en silencio.
//
// Se copia a un staging y recien al final se renombra: si el proceso muere a
// mitad, el destino no existe y el proximo arranque reintenta.
//
// La carpeta vieja NO se borra: son unos KB y queda como rollback gratis.
void migrateLegacyAppDataDir()
{
    const QString currentDir = appDataDirFor(QStringLiteral("VideoDownloader"));
    const QString legacyDir = appDataDirFor(QStringLiteral("VimeoDownloader"));
    const QString configName = QStringLiteral("config.ini");

    if (QFileInfo::exists(QDir(currentDir).filePath(configName))) {
        return; // Ya migrado, o la app ya guardo su propia config.
    }
    if (!QFileInfo::exists(QDir(legacyDir).filePath(configName))) {
        return; // Instalacion nueva: no hay nada que migrar.
    }

    const QString stagingDir = currentDir + QStringLiteral(".migrating");
    if (QDir(stagingDir).exists() && !QDir(stagingDir).removeRecursively()) {
        qWarning() << "Migracion AppData: no se pudo limpiar el staging previo" << stagingDir;
        return;
    }
    if (!QDir().mkpath(stagingDir)) {
        qWarning() << "Migracion AppData: no se pudo crear" << stagingDir;
        return;
    }

    // Solo los archivos del nivel superior; no se esperan subdirectorios.
    const QFileInfoList entries =
        QDir(legacyDir).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (!QFile::copy(entry.absoluteFilePath(), QDir(stagingDir).filePath(entry.fileName()))) {
            qWarning() << "Migracion AppData: no se pudo copiar" << entry.fileName();
            QDir(stagingDir).removeRecursively();
            return;
        }
    }

    QDir().mkpath(QFileInfo(currentDir).absolutePath());
    // `rename` no pisa un destino existente: si quedo una carpeta vacia de un
    // `mkpath` previo, hay que sacarla antes.
    if (QDir(currentDir).exists() && !QDir(currentDir).removeRecursively()) {
        qWarning() << "Migracion AppData: no se pudo limpiar el destino" << currentDir;
        QDir(stagingDir).removeRecursively();
        return;
    }
    if (!QDir().rename(stagingDir, currentDir)) {
        qWarning() << "Migracion AppData: no se pudo renombrar el staging a" << currentDir;
        QDir(stagingDir).removeRecursively();
        return;
    }
    qInfo() << "Migracion AppData completada:" << legacyDir << "->" << currentDir;
}

// Borra el usuario y la contrasena de Vimeo que versiones anteriores guardaban EN TEXTO
// PLANO en config.ini (grupo [vimeo]). La autenticacion ahora es por cookies del navegador
// y esos datos ya no se leen: dejarlos en disco solo es riesgo. Se limpia tambien el
// config.ini de la carpeta legacy VimeoDownloader, que la migracion conserva como rollback
// y trae una copia de las mismas credenciales. El resto de la configuracion no se toca.
// Idempotente: si no hay grupo [vimeo], no reescribe nada.
void purgeLegacyAccountCredentials()
{
    const QStringList configs = {
        QDir(appDataDirFor(QStringLiteral("VideoDownloader"))).filePath(QStringLiteral("config.ini")),
        QDir(appDataDirFor(QStringLiteral("VimeoDownloader"))).filePath(QStringLiteral("config.ini")),
    };
    for (const QString &configPath : configs) {
        if (!QFileInfo::exists(configPath)) {
            continue;
        }
        QSettings settings(configPath, QSettings::IniFormat);
        if (!settings.childGroups().contains(QStringLiteral("vimeo"))) {
            continue;
        }
        settings.remove(QStringLiteral("vimeo"));
        settings.sync();
        if (settings.status() == QSettings::NoError) {
            qInfo() << "Credenciales de cuenta guardadas eliminadas de" << configPath;
        } else {
            qWarning() << "No se pudieron eliminar las credenciales guardadas de" << configPath;
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Configurar información de la aplicación
    app.setApplicationName("VideoDownloader");
    // Version desde la macro de CMake: fuente unica de verdad en `project()`.
    app.setApplicationVersion(QStringLiteral(VIDEODOWNLOADER_VERSION));
    app.setOrganizationName("LGA");
    app.setOrganizationDomain("lga.com");

    // Debe correr ANTES de que MainWindow construya su QSettings.
    migrateLegacyAppDataDir();
    // Despues de migrar, para limpiar tambien la copia recien movida.
    purgeLegacyAccountCredentials();

    // Auto-registro en el registro compartido de LGA, para que las otras apps sepan donde esta
    // instalada esta y en que version. Va DESPUES de la migracion para no tocar AppData antes de
    // que se mueva la carpeta vieja, y despues de setApplicationName porque el registro se
    // resuelve subiendo desde AppDataLocation y los dos tienen que coincidir. Desde el arbol de
    // build no escribe nada a proposito, para no pisar a la copia instalada: ver
    // ../LGA_Base_QT_C_Py/docs/Doc_Registro_LGA.md.
    LgaRegistry::registerThisApp(QStringLiteral("VideoDownloader"),
                                 QStringLiteral(VIDEODOWNLOADER_VERSION));

    // Cargar fuentes Inter si están disponibles
    QDir fontsDir(":/fonts");
    if (fontsDir.exists()) {
        QStringList fontFiles = fontsDir.entryList(QStringList() << "*.ttf", QDir::Files);
        for (const QString &fontFile : fontFiles) {
            QFontDatabase::addApplicationFont(":/fonts/" + fontFile);
        }
    }
    
    // Cargar y aplicar el tema oscuro.
    //
    // OJO: este open() FALLA a proposito y la app usa el fallback de abajo.
    // El .qrc declara prefix="/styles" con el archivo en "styles/", asi que el
    // recurso real es :/styles/styles/dark_theme.qss y esta ruta no existe.
    // `dark_theme.qss` quedo desactualizado: sus reglas de QGroupBox/QLineEdit
    // rompen el layout. El estilo REAL de la app es ColorUtils::getStyleSheet().
    // Si alguna vez se quiere revivir el .qss, hay que arreglar el .qss primero
    // y recien despues el prefix del .qrc; no al reves.
    QFile styleFile(":/styles/dark_theme.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream stream(&styleFile);
        QString styleSheet = stream.readAll();
        
        // Reemplazar variables de color con valores reales
        styleSheet.replace("bg_principal", ColorUtils::BG_PRINCIPAL);
        styleSheet.replace("txt_principal", ColorUtils::TXT_PRINCIPAL);
        styleSheet.replace("boton_gris_oscuro", ColorUtils::BOTON_GRIS_OSCURO);
        styleSheet.replace("boton_gris_oscu_hover", ColorUtils::BOTON_GRIS_OSCU_HOVER);
        styleSheet.replace("border_principal", ColorUtils::BORDER_PRINCIPAL);
        
        app.setStyleSheet(styleSheet);
        styleFile.close();
    } else {
        qDebug() << "No se pudo cargar el archivo de estilos";
        // Aplicar estilo básico como fallback
        app.setStyleSheet(ColorUtils::getStyleSheet());
    }
    
    // Crear y mostrar la ventana principal
    MainWindow window;
    window.show();
    
    return app.exec();
}

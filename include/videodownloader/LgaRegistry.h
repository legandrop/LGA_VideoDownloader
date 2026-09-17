#pragma once

#include <QString>

/**
 * Registro COMPARTIDO entre las apps LGA.
 *
 * Que es y por que existe
 * -----------------------
 * Las apps LGA necesitan saber unas de otras dos cosas: donde esta instalada cada app, y cual
 * es la carpeta `.nuke` del usuario. El card de LGA Updates de PipeSync es el primer consumidor
 * —chequea que hay instalado y que version—, pero el dato no es suyo: lo sabe cada app sobre si
 * misma. Por eso se escribe en un lugar comun, en CLARO, y no en el config privado de nadie.
 *
 * Formato
 * -------
 * Un JSON por app, mas uno para la carpeta de Nuke, todos bajo el mismo directorio:
 *
 *   macOS    ~/Library/Application Support/LGA/
 *   Windows  %APPDATA%/LGA/                        (o sea AppData/Roaming/LGA)
 *
 *   nuke.json          { "nukeDir": "/Users/x/.nuke", "updatedAt": "<ISO-8601>" }
 *   <AppName>.json     { "name": "...", "version": "...", "installPath": "...",
 *                        "executable": "...", "updatedAt": "<ISO-8601>" }
 *
 * `installPath` es la carpeta que CONTIENE la app: en macOS el `.app` (no el binario de adentro),
 * en Windows el directorio de instalacion. Es lo que sirve para ubicar o relanzar la app.
 *
 * Decisiones que conviene no revisar dos veces
 * -------------------------------------------
 * - **En claro, no encriptado.** Son rutas, no credenciales. Un `SecureConfig` acopla a las apps
 *   por una clave y un esquema privados, y si una cambia el formato la otra se rompe en silencio.
 * - **La app se auto-registra al arrancar**, en vez de que lo escriba el instalador. Funciona
 *   igual en las dos plataformas, sobrevive a que el usuario mueva la app de lugar, y no depende
 *   de que el instalador se haya corrido (en macOS directamente no hay instalador).
 * - **Menos desde una salida de desarrollo.** Como se escribe en cada arranque, el ultimo
 *   binario abierto gana, y en desarrollo ese es siempre el del build: el card de LGA Updates
 *   terminaba mostrando la ruta del arbol de compilacion en vez de la instalada. Es lo UNICO
 *   que no registra, y se reconoce de dos formas: por el NOMBRE de la carpeta que contiene la
 *   app (`build`, `deploy`, ... — solo esa carpeta, no la ruta entera) y por las rutas reales
 *   del proyecto que inyecta CMake (`LGA_BUILD_TREE_DIR`, `LGA_SOURCE_TREE_DIR`).
 * - **Escritura atomica** (archivo temporal + rename): dos apps LGA pueden arrancar a la vez, y
 *   un lector no puede encontrarse un JSON a medio escribir.
 * - **Fallar no molesta al usuario.** Si el registro no se puede escribir, la app sigue andando:
 *   lo unico que se pierde es que otra app la vea. Va al log y nada mas.
 *
 * Como se cablea en una app nueva
 * -------------------------------
 * 1. Copiar este par de archivos.
 * 2. En el `CMakeLists.txt`, agregarlos a las fuentes y definir los dos arboles de desarrollo:
 *
 *        target_compile_definitions(<target> PRIVATE
 *            LGA_BUILD_TREE_DIR="${CMAKE_BINARY_DIR}"
 *            LGA_SOURCE_TREE_DIR="${CMAKE_SOURCE_DIR}"
 *        )
 *
 * 3. En `main()`, DESPUES de `setOrganizationName("LGA")` y `setApplicationName(...)`:
 *
 *        LgaRegistry::registerThisApp(QStringLiteral("<AppName>"), <version>);
 *
 * Las dos llamadas de Qt son obligatorias: `AppDataLocation` saltea los componentes vacios, asi
 * que sin `applicationName` la ruta ya seria `.../LGA` y el registro terminaria un nivel mas
 * arriba, sin error y sin log.
 *
 * Doc completa: ../LGA_Base_QT_C_Py/docs/Doc_Registro_LGA.md
 */
namespace LgaRegistry {

/** Directorio del registro. Lo crea si no existe. Vacio si no se pudo resolver. */
QString directory();

/**
 * Registra esta app: nombre visible, version e `installPath` (deducido del ejecutable que
 * corre). Se llama una vez al arrancar.
 *
 * NO se registra si el binario corre desde una salida de desarrollo: carpeta contenedora
 * llamada `build`/`deploy`, o adentro del arbol de build o del repo. Cualquier otra ubicacion
 * si: `/Applications`, `Program Files`, o donde el usuario la haya puesto. Es una lista negra a
 * proposito — una lista blanca de ubicaciones "validas" dejaria sin registrar instalaciones
 * legitimas.
 *
 * Devuelve false si no se pudo escribir Y TAMBIEN si se saltea por correr desde el arbol de
 * desarrollo; en los dos casos queda la razon en el log. Ningun llamador deberia tratar el
 * false como error.
 */
bool registerThisApp(const QString& appName, const QString& version);

/**
 * Si el binario corre desde una salida de desarrollo (mismo criterio que registerThisApp).
 * Lo usa ademas el auto-update de la app para no reemplazar un build por un instalador.
 */
bool isDevelopmentBuild();

/** Guarda la carpeta `.nuke` que el usuario eligio, para que la vean las demas apps LGA. */
bool saveNukeDirectory(const QString& nukeDir);

/** Lee la carpeta `.nuke` registrada. String vacio si no hay ninguna. */
QString readNukeDirectory();

} // namespace LgaRegistry

#!/bin/bash

# El .app y el ejecutable se llaman "LGA Video Downloader" (convencion LGA: el nombre visible
# arranca con "LGA" para que todas las apps queden juntas en /Applications). El nombre de
# ARCHIVO de los artefactos es otro y va sin espacios, porque viaja por URL en los releases.
APP_NAME="LGA Video Downloader"
ARTIFACT_NAME="LGA_Video_Downloader"

show_help() {
    echo "Uso: $0 [--zip] [--dmg] [--no-install] [--no-open] [--publish [--dry-run] [--replace] [--yes] | --no-publish]"
    echo "  --zip         Crear deploy/${ARTIFACT_NAME}_Mac_v<version>.zip firmado (actualizacion)"
    echo "  --dmg         Crear deploy/${ARTIFACT_NAME}_Mac_v<version>.dmg (primera instalacion)"
    echo "  --no-install  No instalar en /Applications ni abrir la app (deja solo deploy/)."
    echo "                --no-run es un sinonimo, por compatibilidad"
    echo "  --no-open     Instalar en /Applications pero no abrir la app"
    echo "  --publish     Crear el .zip y el .dmg y publicarlos en el release v<version> de"
    echo "                legandrop/LGA_VideoDownloader, con SHA256SUMS y las notas (What's new)"
    echo "  --no-publish  No publicar ni preguntar"
    echo "  --dry-run     Con --publish: todo menos escribir en GitHub y en el repo"
    echo "  --replace     Con --publish: reemplazar los archivos de macOS si el release ya los tiene"
    echo "  --yes         Con --publish: no preguntar en el control de las notas"
    echo ""
    echo "Sin flags y con terminal interactiva, mira GitHub y pregunta primero, con una tecla: publicar"
    echo "(o sumar macOS al release), build local o cancelar. Sin terminal (pipe, CI) no pregunta"
    echo "nada: hay que pasar los flags."
}

CREATE_ZIP=false
CREATE_DMG=false
INSTALL_APP=true
OPEN_APP=true
# "prompt" = preguntar solo sin flags y con terminal: un script sin tty no se cuelga esperando input.
PUBLISH_MODE="prompt"
PUBLISH_ARGS=()
ARG_COUNT=$#
for arg in "$@"; do
    case "$arg" in
        --zip) CREATE_ZIP=true ;;
        --dmg) CREATE_DMG=true ;;
        --no-install|--no-run) INSTALL_APP=false ;;
        --no-open) OPEN_APP=false ;;
        --publish)
            [ "$PUBLISH_MODE" = "never" ] && { echo "ERROR: --publish y --no-publish se contradicen."; exit 1; }
            PUBLISH_MODE="always" ;;
        --no-publish)
            [ "$PUBLISH_MODE" = "always" ] && { echo "ERROR: --publish y --no-publish se contradicen."; exit 1; }
            PUBLISH_MODE="never" ;;
        --dry-run|--replace|--yes) PUBLISH_ARGS+=("$arg") ;;
        -h|--help) show_help; exit 0 ;;
        # Una opcion mal escrita corta: un `--no-instal` no puede terminar instalando y abriendo.
        *) echo "ERROR: opcion desconocida: $arg"; show_help; exit 1 ;;
    esac
done

# Version UNICA: sale del CMakeLists. Antes el Info.plist la traia hardcodeada y quedo
# desfasada del proyecto.
APP_VERSION="$(sed -n 's/^project(VideoDownloader VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
if [ -z "$APP_VERSION" ]; then
    echo "Error: no se pudo leer la version del CMakeLists.txt"
    exit 1
fi
echo "Version: $APP_VERSION"

# Publicar se decide ANTES de compilar. Sin flags y con terminal, `publish_release.sh --decide`
# (pieza canonica de la Base) mira el release en GitHub y pregunta con UNA tecla: publicar o sumar
# macOS al release que ya existe (lo pudo crear Windows), build local o cancelar. Esta app no tiene
# bump automatizado (BUMP_CMD vacio en tools/macos/release.conf): si la version ya esta publicada o
# el changelog va adelante, lo dice y no ofrece publicar. Si vuelve con "publicar", el preflight ya
# paso. Si se elige local, sigue el deploy de siempre (compila, instala y abre).
PUBLISH=false
DECIDED=false
if [ "$PUBLISH_MODE" = "always" ]; then
    PUBLISH=true
elif [ "$PUBLISH_MODE" = "prompt" ] && [ "$ARG_COUNT" -eq 0 ] && [ -t 0 ]; then
    DECIDE_RC=0
    bash "./tools/macos/publish_release.sh" --decide || DECIDE_RC=$?
    case "$DECIDE_RC" in
        0) PUBLISH=true; DECIDED=true ;;
        10) ;;
        20)
            echo "Cancelado. No se compilo nada."
            exit 1
            ;;
        *) echo "AVISO: no se pudo decidir la publicacion (ver arriba): se sigue con el build local." ;;
    esac
fi
if [ "$PUBLISH" != "true" ] && [ ${#PUBLISH_ARGS[@]} -gt 0 ]; then
    echo "ERROR: ${PUBLISH_ARGS[*]} solo tiene sentido con --publish."
    exit 1
fi

# Publicar es tools/macos/publish_release.sh, la pieza canonica de LGA_Base_QT_C_Py (copia IDENTICA;
# lo propio de esta app esta en tools/macos/release.conf). Se llama dos veces: --preflight ACA, antes
# de borrar el deploy y compilar (gh y la identidad, arbol limpio, VERSION igual a CMakeLists.txt, el
# tag, el release y las notas; junta todos los problemas y corta sin gastar el build), y --expect-head
# con el .zip y el .dmg ya armados (vuelve a comprobar todo, porque Windows pudo publicar en el medio,
# y publica). Publicar implica el .zip y el .dmg. Contrato: ../LGA_Base_QT_C_Py/docs/Doc_Deploy_macOS.md §5.
PUBLISH_HEAD=""
if [ "$PUBLISH" = "true" ]; then
    CREATE_ZIP=true
    CREATE_DMG=true
    PUBLISH_HEAD="$(git rev-parse HEAD)"
    # Con --decide el preflight ya corrio: no se repite.
    if [ "$DECIDED" != "true" ]; then
        bash "./tools/macos/publish_release.sh" --preflight ${PUBLISH_ARGS[@]+"${PUBLISH_ARGS[@]}"} || exit 1
    fi
fi

# BORRAR DEPLOY ANTERIOR
if [ -d "deploy" ]; then
    echo "Eliminando deploy anterior..."
    rm -rf deploy
fi

echo "Implementando VideoDownloader..."

# Matar procesos previos si están en ejecución.
# IMPORTANTE: no usar `pkill -f VideoDownloader` (patrón demasiado genérico:
# matchea procesos de extensiones de VSCode con `--folder-uri` al
# workspace `LGA_VideoDownloader` y provoca que VSCode los relance varias
# veces al arrancar el script). Apuntar al path completo del ejecutable
# dentro del .app.
pkill -f "${APP_NAME}.app/Contents/MacOS/${APP_NAME}" 2>/dev/null && echo "   - VideoDownloader terminado" || echo "   - VideoDownloader no estaba en ejecución"
sleep 1

# Verificar que Qt de Homebrew está instalado (compatible con macOS Tahoe)
QT_PATH="/opt/homebrew"
if [ ! -d "$QT_PATH" ]; then
    echo "Error: Qt no está instalado en $QT_PATH (Homebrew)"
    exit 1
fi

# Configurar variables de entorno para Qt de Homebrew
export CMAKE_PREFIX_PATH="$QT_PATH"
export Qt6_DIR="$QT_PATH/lib/cmake/Qt6"
export PATH="$QT_PATH/bin:$PATH"
SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)"
if [ -d "$SDK_PATH" ]; then
    export SDKROOT="$SDK_PATH"
else
    echo "Advertencia: SDK de macOS no encontrado vía xcrun."
fi

# Crear directorio de implementación si no existe
mkdir -p deploy

# Compilar el proyecto en modo Release con configuraciones de compatibilidad
mkdir -p build
cd build

# Configurar el proyecto con Qt de Homebrew (compatible con macOS Tahoe)
# Usar solo arquitectura ARM64 ya que Qt de Homebrew solo soporta ARM64
cmake .. -G "Unix Makefiles" \
    -DCMAKE_PREFIX_PATH="$QT_PATH" \
    -DQt6_DIR="$QT_PATH/lib/cmake/Qt6" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DCMAKE_OSX_ARCHITECTURES="arm64" \
    -DCMAKE_OSX_SYSROOT="$SDK_PATH"

if ! cmake --build . --config Release; then
    echo "ERROR: fallo la compilacion. No se empaqueta ni se publica nada."
    exit 1
fi
cd ..

# Crear estructura del bundle
mkdir -p "deploy/${APP_NAME}.app/Contents"/{MacOS,Resources,Frameworks}
if ! cp "build/${APP_NAME}.app/Contents/MacOS/${APP_NAME}" "deploy/${APP_NAME}.app/Contents/MacOS/"; then
    echo "ERROR: no se pudo copiar el ejecutable al bundle. No se empaqueta ni se publica nada."
    exit 1
fi

# Copiar el ícono al bundle si existe
if [ -f "resources/icons/LGA_VideoDownloader.icns" ]; then
    echo "Copiando ícono al bundle..."
    cp resources/icons/LGA_VideoDownloader.icns "deploy/${APP_NAME}.app/Contents/Resources/"
fi

# Crear Info.plist con configuración mejorada de compatibilidad
#
# DEUDA CONOCIDA: esto PISA el Info.plist que ya genero CMake desde cmake/Info.plist.in, asi
# que hay DOS copias de la misma informacion y hay que mantener las dos. Es exactamente lo
# que causo el bug del versionado: se arreglo `CFBundleShortVersionString` en una copia y
# `CFBundleVersion` quedo en 0.86 en la otra, y el bundle publicado salio con las dos claves
# discrepando durante tres versiones.
#
# Existe porque este heredoc agrega claves de compatibilidad que el template no trae
# (LSMinimumSystemVersion, LSArchitecturePriority, NSAppTransportSecurity,
# NSSupportsAutomaticGraphicsSwitching). Lo correcto es moverlas al template y que el deploy
# NO toque el plist. Al tocar cualquier clave de aca, revisar cmake/Info.plist.in tambien.
cat > "deploy/${APP_NAME}.app/Contents/Info.plist" << EOL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIconFile</key>
    <string>LGA_VideoDownloader</string>
    <key>CFBundleIdentifier</key>
    <string>com.lga.videodownloader</string>
    <key>CFBundleName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleDisplayName</key>
    <string>${APP_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleVersion</key>
    <string>${APP_VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${APP_VERSION}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.0.0</string>
    <key>LSArchitecturePriority</key>
    <array>
        <string>arm64</string>
        <string>x86_64</string>
    </array>
    <key>LSRequiresNativeExecution</key>
    <false/>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.utilities</string>
    <key>NSAppTransportSecurity</key>
    <dict>
        <key>NSAllowsArbitraryLoads</key>
        <true/>
    </dict>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>
    <key>NSHumanReadableCopyright</key>
    <string>© 2024 LGA. Todos los derechos reservados.</string>
</dict>
</plist>
EOL

# macdeployqt + fixup: el bundle tiene que ser AUTOCONTENIDO.
# macdeployqt solo no alcanza con el Qt de Homebrew —que parte Qt en un keg por modulo y
# arrastra dependencias entre ellos que la herramienta no persigue: sin QtDBus el binario
# muere en dyld, y quedan QtSvg, QtPdf y las QtVirtualKeyboard declaradas como @rpath pero
# nunca copiadas—. `tools/macos/bundle_fixup.py` completa eso y VERIFICA que no quede
# ninguna referencia fuera del bundle. Ver docs.
echo ""
echo "Ejecutando macdeployqt..."
"$QT_PATH/opt/qtbase/bin/macdeployqt" "deploy/${APP_NAME}.app" >/dev/null 2>&1 || true

echo "Completando dependencias del bundle..."
if ! python3 tools/macos/bundle_fixup.py "deploy/${APP_NAME}.app"; then
    echo "ERROR: el bundle quedo con dependencias fuera de el; no se puede distribuir asi."
    exit 1
fi

# Crear carpeta toolsmac en deploy y copiar herramientas
echo ""
echo "Preparando carpeta toolsmac para deploy..."
if [ -d "toolsmac" ]; then
    echo "Copiando herramientas a carpeta deploy..."
    cp -r toolsmac "deploy/${APP_NAME}.app/Contents/MacOS/"
    echo "Herramientas copiadas exitosamente."
else
    echo "Carpeta toolsmac no encontrada o vacía."
fi

# Extension de navegador (no verificado en Mac): va en Resources ANTES de firmar, porque la
# firma cubre el contenido. Al arrancar, la app la copia a Application Support y escribe el JSON
# del host (path absoluto) en NativeMessagingHosts de Chrome, Brave y Edge.
rm -rf "deploy/${APP_NAME}.app/Contents/Resources/extension"
cp -R extension "deploy/${APP_NAME}.app/Contents/Resources/extension"

# Hacer ejecutable el script
chmod +x "deploy/${APP_NAME}.app/Contents/MacOS/${APP_NAME}"

# Firma ad-hoc del bundle YA armado: la firma cubre el contenido, asi que va al final. No
# es notarizacion ni confianza de Gatekeeper (sigue haciendo falta el `xattr -cr`): sirve
# para poder verificar con `codesign --verify` que el bundle llego entero.
echo "Firmando el bundle (ad-hoc)..."
if ! codesign --force --deep --sign - "deploy/${APP_NAME}.app"; then
    echo "ERROR: fallo la firma del bundle. No se empaqueta ni se publica nada."
    exit 1
fi
bash "./tools/macos/verify_bundle_signature.sh" "deploy/${APP_NAME}.app" || exit 1

if [ "$CREATE_ZIP" = "true" ]; then
    ZIP_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.zip"
    # ditto y NO zip: `zip -r` RESUELVE los symlinks en vez de guardarlos, y un .app de Qt
    # esta lleno (Versions/Current, el binario de cada framework). Con zip el bundle llega
    # al usuario mucho mas pesado, con cada framework duplicado, y la firma invalida.
    rm -f "deploy/${ZIP_NAME}"
    # Antes y despues: el .app falla si alguien lo ejecuto despues de firmarlo; el zip, si el
    # empaquetado no conserva el bundle tal cual. El zip es lo que baja el auto-update.
    bash "./tools/macos/verify_bundle_signature.sh" "deploy/${APP_NAME}.app" || exit 1
    if ! (cd deploy && ditto -c -k --sequesterRsrc --keepParent "${APP_NAME}.app" "${ZIP_NAME}"); then
        echo "ERROR: no se pudo crear el .zip. No se publica nada."
        exit 1
    fi
    if ! bash "./tools/macos/verify_bundle_signature.sh" "deploy/${ZIP_NAME}"; then
        rm -f "deploy/${ZIP_NAME}"
        echo "ERROR: el .zip no pasa la verificacion de firma. No se publica nada."
        exit 1
    fi
    echo "ZIP creado: deploy/${ZIP_NAME}"
fi

# El DMG es el artefacto de PRIMERA INSTALACION; el ZIP de arriba es el de actualizacion y
# los dos no son intercambiables. Ver ../LGA_Base_QT_C_Py/docs/Doc_Deploy_macOS.md.
if [ "$CREATE_DMG" = "true" ]; then
    rm -f "deploy/${ARTIFACT_NAME}_Mac_v${APP_VERSION}.dmg"
    if ! bash ./create_dmg.sh --no-open; then
        echo "ERROR: no se pudo crear el .dmg. No se publica nada."
        exit 1
    fi
fi


# SHA256SUMS de macOS: el auto-update de la app no instala nada sin su hash. Formato sha256sum
# ("hash  nombre"). Al publicar, publish_release.sh lo rehace y lo fusiona con el del release, que
# lleva TAMBIEN la linea del instalador de Windows (installer/SHA256SUMS de instalador.bat).
if [ "$CREATE_ZIP" = "true" ] || [ "$CREATE_DMG" = "true" ]; then
    (
        cd deploy
        rm -f SHA256SUMS
        for artifact in "${ARTIFACT_NAME}_Mac_v${APP_VERSION}.zip" "${ARTIFACT_NAME}_Mac_v${APP_VERSION}.dmg"; do
            if [ -f "$artifact" ]; then
                shasum -a 256 "$artifact" >> SHA256SUMS
            fi
        done
    )
    echo "SHA256SUMS creado: deploy/SHA256SUMS"
fi

# Si la publicacion falla, igual se instala la app (el build esta bien) y el script sale con error.
PUBLISH_FAILED=false
if [ "$PUBLISH" = "true" ]; then
    if ! bash "./tools/macos/publish_release.sh" --expect-head "$PUBLISH_HEAD" ${PUBLISH_ARGS[@]+"${PUBLISH_ARGS[@]}"}; then
        PUBLISH_FAILED=true
    fi
fi

echo
echo "Implementación completada. La aplicación portable está en la carpeta '"deploy/${APP_NAME}.app"'."
echo

# El final de todo deploy: instalar en /Applications y abrir la copia INSTALADA. El .app de
# deploy/ no se ejecuta nunca: es el que se empaqueta, y ejecutarlo le rompe la firma. Se abre
# con `open`, asi que tampoco retiene la terminal.
if [ "$INSTALL_APP" = "true" ]; then
    INSTALL_ARGS=()
    [ "$OPEN_APP" = "false" ] && INSTALL_ARGS+=(--no-open)
    bash "./tools/macos/install_to_applications.sh" "deploy/${APP_NAME}.app" ${INSTALL_ARGS[@]+"${INSTALL_ARGS[@]}"} || exit 1
else
    echo "Sin instalar (--no-install)."
fi

if [ "$PUBLISH_FAILED" = "true" ]; then
    echo "ERROR: la publicacion del release v${APP_VERSION} fallo (ver arriba)."
    [ "$INSTALL_APP" = "true" ] && echo "       La app quedo instalada igual."
    exit 1
fi

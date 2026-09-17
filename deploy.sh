#!/bin/bash

# El .app y el ejecutable se llaman "LGA Video Downloader" (convencion LGA: el nombre visible
# arranca con "LGA" para que todas las apps queden juntas en /Applications). El nombre de
# ARCHIVO de los artefactos es otro y va sin espacios, porque viaja por URL en los releases.
APP_NAME="LGA Video Downloader"
ARTIFACT_NAME="LGA_Video_Downloader"

CREATE_ZIP=false
CREATE_DMG=false
NO_RUN=false
for arg in "$@"; do
    case "$arg" in
        --zip) CREATE_ZIP=true ;;
        --dmg) CREATE_DMG=true ;;
        --no-run) NO_RUN=true ;;
        -h|--help)
            echo "Uso: $0 [--zip] [--dmg] [--no-run]"
            echo "  --zip     Crear deploy/${ARTIFACT_NAME}_Mac_v<version>.zip firmado (actualizacion)"
            echo "  --dmg     Crear deploy/${ARTIFACT_NAME}_Mac_v<version>.dmg (primera instalacion)"
            echo "  --no-run  No ejecutar la app al terminar"
            exit 0
            ;;
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

cmake --build . --config Release
cd ..

# Crear estructura del bundle
mkdir -p "deploy/${APP_NAME}.app/Contents"/{MacOS,Resources,Frameworks}
cp "build/${APP_NAME}.app/Contents/MacOS/${APP_NAME}" "deploy/${APP_NAME}.app/Contents/MacOS/"

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
codesign --force --deep --sign - "deploy/${APP_NAME}.app"

if [ "$CREATE_ZIP" = "true" ]; then
    ZIP_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.zip"
    # ditto y NO zip: `zip -r` RESUELVE los symlinks en vez de guardarlos, y un .app de Qt
    # esta lleno (Versions/Current, el binario de cada framework). Con zip el bundle llega
    # al usuario mucho mas pesado, con cada framework duplicado, y la firma invalida.
    rm -f "deploy/${ZIP_NAME}"
    (cd deploy && ditto -c -k --sequesterRsrc --keepParent "${APP_NAME}.app" "${ZIP_NAME}")
    echo "ZIP creado: deploy/${ZIP_NAME}"
fi

# El DMG es el artefacto de PRIMERA INSTALACION; el ZIP de arriba es el de actualizacion y
# los dos no son intercambiables. Ver ../LGA_Base_QT_C_Py/docs/Doc_Deploy_macOS.md.
if [ "$CREATE_DMG" = "true" ]; then
    rm -f "deploy/${ARTIFACT_NAME}_Mac_v${APP_VERSION}.dmg"
    bash ./create_dmg.sh --no-open
fi

# SHA256SUMS del release: el auto-update de la app no instala nada sin su hash. Formato
# sha256sum ("hash  nombre"). Al publicar, el SHA256SUMS del release tiene que llevar TAMBIEN
# las lineas del instalador de Windows (installer/SHA256SUMS de instalador.bat).
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

echo
echo "Implementación completada. La aplicación portable está en la carpeta '"deploy/${APP_NAME}.app"'."
echo

if [ "$NO_RUN" = "true" ]; then
    echo "Omitiendo ejecución (--no-run)."
else
    # Sin QT_QPA_PLATFORM_PLUGIN_PATH: el bundle trae sus propios plugins. Si hiciera falta
    # apuntar a los de Homebrew, es que el bundle NO quedo autocontenido.
    echo "Ejecutando VideoDownloader..."
    "./deploy/${APP_NAME}.app/Contents/MacOS/${APP_NAME}"
fi

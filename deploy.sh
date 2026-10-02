#!/bin/bash

# El .app y el ejecutable se llaman "LGA Video Downloader" (convencion LGA: el nombre visible
# arranca con "LGA" para que todas las apps queden juntas en /Applications). El nombre de
# ARCHIVO de los artefactos es otro y va sin espacios, porque viaja por URL en los releases.
APP_NAME="LGA Video Downloader"
ARTIFACT_NAME="LGA_Video_Downloader"

CREATE_ZIP=false
CREATE_DMG=false
NO_RUN=false
PUBLISH=false
REPLACE=false
for arg in "$@"; do
    case "$arg" in
        --zip) CREATE_ZIP=true ;;
        --dmg) CREATE_DMG=true ;;
        --no-run) NO_RUN=true ;;
        --publish) PUBLISH=true; CREATE_ZIP=true; CREATE_DMG=true ;;
        --replace) REPLACE=true ;;
        -h|--help)
            echo "Uso: $0 [--zip] [--dmg] [--no-run] [--publish [--replace]]"
            echo "  --zip      Crear deploy/${ARTIFACT_NAME}_Mac_v<version>.zip firmado (actualizacion)"
            echo "  --dmg      Crear deploy/${ARTIFACT_NAME}_Mac_v<version>.dmg (primera instalacion)"
            echo "  --no-run   No ejecutar la app al terminar"
            echo "  --publish  Crear el .zip y el .dmg y publicarlos en el release v<version> de"
            echo "             legandrop/LGA_VideoDownloader, con SHA256SUMS y las notas (What's new)"
            echo "  --replace  Con --publish: reemplazar los archivos de macOS si el release ya los tiene"
            exit 0
            ;;
    esac
done

RELEASE_REPO="legandrop/LGA_VideoDownloader"
# LGA_GH apunta a otro gh, igual que en el helper de las notas.
GH="${LGA_GH:-gh}"

# ==== PUBLICAR: inicio. El banco de pruebas extrae lo que hay entre estas marcas: no moverlas.

# SHA256SUMS del release: las lineas que NO son de esta plataforma, mas las propias al final. Una
# sola linea por nombre de archivo; formato sha256sum ("hash  nombre"), LF. Una linea ilegible
# corta: no se adivina que era. Sin intervalos {64} en las regex: el awk de macOS no siempre los
# entiende.
merge_sha256sums() {  # <SHA256SUMS del release, o /dev/null> <el propio> <salida>
    if ! grep -q . "$2"; then
        echo "ERROR: $2 esta vacio."
        return 1
    fi
    awk '
        { sub(/\r$/, "") }
        length($0) > 66 && substr($0, 1, 64) ~ /^[0-9a-fA-F]+$/ && substr($0, 65, 1) == " " && substr($0, 66, 1) ~ /[ *]/ {
            name = substr($0, 67)
            if (FNR == NR) { if (!(name in own)) { own[name] = 1; mine[++m] = $0 } }
            else if (!(name in own) && !(name in seen)) { seen[name] = 1; keep[++k] = $0 }
            next
        }
        NF { print "ERROR: el SHA256SUMS del release tiene una linea ilegible: " $0 > "/dev/stderr"; bad = 1; exit 1 }
        END { if (bad) exit 1; for (i = 1; i <= k; i++) print keep[i]; for (i = 1; i <= m; i++) print mine[i] }
    ' "$2" "$1" > "$3"
}

# Lo que hay en el release v<version> antes de tocarlo. Corre antes de compilar y otra vez al
# publicar, porque Windows pudo publicar en el medio. Corta si el release es un borrador, si ya
# tiene los archivos de macOS (salvo --replace) o si tiene el instalador de Windows sin
# SHA256SUMS: fusionar contra nada borraria su linea y el auto-update de Windows dejaria de verlo.
# Deja REL_EXISTS y REL_SUMS para publish_release.
release_check() {
    local tag="v${APP_VERSION}" names draft
    REL_EXISTS=false
    REL_SUMS=false
    "$GH" release view "$tag" --repo "$RELEASE_REPO" >/dev/null 2>&1 || return 0
    REL_EXISTS=true
    draft="$("$GH" release view "$tag" --repo "$RELEASE_REPO" --json isDraft -q '.isDraft')" \
        || { echo "ERROR: no se pudo leer el release $tag de $RELEASE_REPO."; return 1; }
    names="$("$GH" release view "$tag" --repo "$RELEASE_REPO" --json assets -q '.assets[].name')" \
        || { echo "ERROR: no se pudo leer el release $tag de $RELEASE_REPO."; return 1; }
    names="$(printf '%s\n' "$names" | tr -d '\r')"
    draft="$(printf '%s\n' "$draft" | tr -d '\r')"
    if printf '%s\n' "$draft" | grep -qx 'true'; then
        echo "ERROR: el release $tag existe como borrador. Publicarlo o borrarlo a mano antes de seguir."
        return 1
    fi
    if printf '%s\n' "$names" | grep -qx 'SHA256SUMS'; then
        REL_SUMS=true
    fi
    if printf '%s\n' "$names" | grep -q "^${ARTIFACT_NAME}_Mac_v" && [ "$REPLACE" != "true" ]; then
        echo "ERROR: el release $tag ya tiene los archivos de macOS. Para reemplazarlos, correr con --replace."
        return 1
    fi
    if printf '%s\n' "$names" | grep -q '^VideoDownloader_Setup_v' && [ "$REL_SUMS" != "true" ]; then
        echo "ERROR: el release $tag tiene el instalador de Windows pero no SHA256SUMS: no se fusiona"
        echo "       contra nada, porque se perderia su linea. Subir primero el SHA256SUMS de Windows."
        return 1
    fi
    return 0
}

publish_failed() {  # <carpeta temporal>
    echo "ERROR: fallo la publicacion del release v${APP_VERSION}. Ver el mensaje de arriba."
    rm -rf "$1"
}

# El release v<version> vive en este mismo repo. Lo crea la primera plataforma que publica; la
# otra (instalador.bat --publish en Windows) lo encuentra y suma lo suyo. Todo lo que puede cortar
# (leer, fusionar) pasa antes de escribir nada. El .zip y el .dmg suben ANTES que el SHA256SUMS:
# en el medio la app no ofrece nada que no pueda verificar.
publish_release() {
    local tag="v${APP_VERSION}" work old a sums=deploy/release/SHA256SUMS
    local assets=()
    for a in "deploy/${ARTIFACT_NAME}_Mac_v${APP_VERSION}.zip" "deploy/${ARTIFACT_NAME}_Mac_v${APP_VERSION}.dmg"; do
        [ -f "$a" ] || { echo "ERROR: falta $a"; return 1; }
        assets+=("$a")
    done
    release_check || return 1
    if [ "$REL_EXISTS" != "true" ]; then
        echo "Creando el release $tag en $RELEASE_REPO..."
        "$GH" release create "$tag" "${assets[@]}" deploy/SHA256SUMS --repo "$RELEASE_REPO" \
            --target "$HEAD_SHA" --title "$tag" --notes "LGA Video Downloader $tag" \
            || { publish_failed ""; return 1; }
    else
        echo "El release $tag ya existe: se suman el .zip y el .dmg de macOS."
        work="$(mktemp -d)" || return 1
        old=/dev/null
        if [ "$REL_SUMS" = "true" ]; then
            "$GH" release download "$tag" --repo "$RELEASE_REPO" --pattern SHA256SUMS --dir "$work" \
                || { publish_failed "$work"; return 1; }
            old="$work/SHA256SUMS"
        fi
        # El fusionado queda en una ruta fija: si la subida falla (--clobber borra el viejo antes
        # de subir), se resube a mano desde ahi.
        mkdir -p deploy/release
        merge_sha256sums "$old" deploy/SHA256SUMS "$sums" || { publish_failed "$work"; return 1; }
        echo "SHA256SUMS fusionado: $(wc -l < "$sums" | tr -d ' ') lineas"
        "$GH" release upload "$tag" "${assets[@]}" --repo "$RELEASE_REPO" --clobber \
            || { publish_failed "$work"; return 1; }
        if ! "$GH" release upload "$tag" "$sums" --repo "$RELEASE_REPO" --clobber; then
            echo "ERROR: el .zip y el .dmg subieron, pero SHA256SUMS no: el release puede haber quedado"
            echo "       SIN SHA256SUMS y la app no ofrece el update. El fusionado esta en $(pwd)/$sums."
            echo "       Subirlo con: gh release upload $tag \"$(pwd)/$sums\" --repo $RELEASE_REPO --clobber"
            rm -rf "$work"
            return 1
        fi
        rm -rf "$work"
    fi
    echo "Publicando las notas para el usuario (What's new)..."
    if ! sh "$WHATS_NEW_SH" publish "$WHATS_NEW_FILE" "$APP_VERSION" "$RELEASE_REPO" "$tag"; then
        echo "ERROR: el release $tag quedo publicado, pero sus notas no. Reintentar con el comando de arriba."
        return 1
    fi
    echo "Release publicado: https://github.com/${RELEASE_REPO}/releases/tag/${tag}"
}

# ==== PUBLICAR: fin

# Version UNICA: sale del CMakeLists. Antes el Info.plist la traia hardcodeada y quedo
# desfasada del proyecto.
APP_VERSION="$(sed -n 's/^project(VideoDownloader VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
if [ -z "$APP_VERSION" ]; then
    echo "Error: no se pudo leer la version del CMakeLists.txt"
    exit 1
fi
echo "Version: $APP_VERSION"

# Publicacion: lo que puede cortarla se chequea ACA, antes de borrar el deploy y compilar.
if [ "$PUBLISH" = "true" ]; then
    if ! command -v "$GH" >/dev/null 2>&1 || ! "$GH" auth status >/dev/null 2>&1; then
        echo "ERROR: --publish necesita gh (GitHub CLI) instalado y con login (gh auth login)."
        exit 1
    fi
    # El tag se crea sobre HEAD (--target): tiene que ser el codigo commiteado y estar en origin.
    if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
        echo "ERROR: hay cambios sin commitear; el release tiene que salir de un commit:"
        git status --short --untracked-files=no
        exit 1
    fi
    git fetch -q origin || { echo "ERROR: no se pudo leer origin."; exit 1; }
    if ! git merge-base --is-ancestor HEAD origin/main; then
        echo "ERROR: HEAD no esta en origin/main. Pushear antes de publicar."
        exit 1
    fi
    HEAD_SHA="$(git rev-parse HEAD)"
    # VERSION (la que leen los scripts) tiene que coincidir con CMakeLists.txt.
    if ! ./sync_version.sh --check-only; then
        echo "ERROR: VERSION y CMakeLists.txt no coinciden. Correr ./sync_version.sh y commitear."
        exit 1
    fi
    # Notas para el usuario (What's new): la logica vive en LGA_RepoTools (WhatsNew_Shared);
    # LGA_REPOTOOLS apunta a otra copia. Si corta, no se compilo ni se publico nada.
    WHATS_NEW_FILE="$(pwd)/docs/WhatsNew.md"
    WHATS_NEW_SH="${LGA_REPOTOOLS:-$(pwd)/../LGA_RepoTools}/WhatsNew_Mac/whats_new_release.sh"
    if [ ! -f "$WHATS_NEW_SH" ]; then
        echo "ERROR: no encontre $WHATS_NEW_SH."
        echo "Clonar LGA_RepoTools al lado de este repo o definir LGA_REPOTOOLS. Sin notas no se publica."
        exit 1
    fi
    echo "Verificando las notas para el usuario (What's new) de v${APP_VERSION}..."
    if ! sh "$WHATS_NEW_SH" check "$WHATS_NEW_FILE" "$APP_VERSION"; then
        echo "ERROR: faltan o fallan las notas de v${APP_VERSION}, o se contesto que no. No se compilo nada."
        exit 1
    fi
    release_check || { echo "No se compilo ni se publico nada."; exit 1; }
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

if [ "$CREATE_ZIP" = "true" ]; then
    ZIP_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.zip"
    # ditto y NO zip: `zip -r` RESUELVE los symlinks en vez de guardarlos, y un .app de Qt
    # esta lleno (Versions/Current, el binario de cada framework). Con zip el bundle llega
    # al usuario mucho mas pesado, con cada framework duplicado, y la firma invalida.
    rm -f "deploy/${ZIP_NAME}"
    if ! (cd deploy && ditto -c -k --sequesterRsrc --keepParent "${APP_NAME}.app" "${ZIP_NAME}"); then
        echo "ERROR: no se pudo crear el .zip. No se publica nada."
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

# SHA256SUMS del release: el auto-update de la app no instala nada sin su hash. Formato
# sha256sum ("hash  nombre"). Al publicar, el SHA256SUMS del release tiene que llevar TAMBIEN
# las lineas del instalador de Windows (installer/SHA256SUMS de instalador.bat): publish_release
# las fusiona.
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

if [ "$PUBLISH" = "true" ]; then
    publish_release || exit 1
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

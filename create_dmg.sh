#!/bin/bash

# Arma el DMG de PRIMERA INSTALACION del template. Las apps LGA derivadas usan este mismo
# esquema; ver docs/Doc_Deploy_macOS.md.
#
# El DMG y el ZIP NO son intercambiables:
#   - DMG: instalacion nueva. Ventana con el .app, el alias a /Applications y el LEEME.
#   - ZIP: artefacto canonico de actualizacion (lo consume el updater de las apps que lo
#     tienen). Lo arma deploy.sh con `ditto`.
#
# Requiere que deploy/<APP>.app ya exista (o sea, correr deploy.sh antes).
#
# EL LAYOUT NO SE ARMA CON APPLESCRIPT. Se probo y falla en silencio: el AppleScript que
# le pide a Finder `set background picture` devuelve EXITO y el `.DS_Store` que termina
# adentro del DMG sale igual sin la imagen, con el fondo en blanco. Peor todavia, al
# montar el DMG en la misma maquina que lo construyo, Finder muestra su estado CACHEADO de
# la sesion en vez de lo que dice el archivo, asi que la verificacion visual da bien y el
# usuario recibe un DMG feo. Por eso se usa `dmgbuild`, que escribe el `.DS_Store`
# directamente, y por eso al final el script VERIFICA el archivo producido.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ---- Lo unico que cambia al derivar una app -------------------------------------------
# Son TRES nombres y no uno solo, porque tienen consumidores distintos:
#
#   APP_NAME       el .app tal cual queda en /Applications, y el ejecutable adentro. Es lo
#                  que ve el usuario en Finder, asi que por convencion LGA arranca con "LGA".
#                  Puede llevar espacios.
#   ARTIFACT_NAME  base del nombre de ARCHIVO del .zip y el .dmg. Sin espacios: viaja por URL
#                  en los releases de GitHub, y un espacio se convierte en %20. Lo consume el
#                  updater, que busca el asset por patron.
#   DISPLAY_NAME   nombre del volumen que monta el DMG y titulo dibujado en el fondo. Los dos
#                  tienen que decir lo mismo.
#
# Ver docs/Doc_Deploy_macOS.md.
APP_NAME="LGA Video Downloader"          # .app y ejecutable — SIEMPRE arranca con "LGA"
ARTIFACT_NAME="LGA_Video_Downloader"     # base del .zip/.dmg — sin espacios
DISPLAY_NAME="LGA Video Downloader"      # nombre visible del volumen y del titulo
# ---------------------------------------------------------------------------------------

show_help() {
    echo "Uso: $0 [--no-open]"
    echo ""
    echo "Crea deploy/${ARTIFACT_NAME}_Mac_v<version>.dmg para una instalacion nueva."
    echo "La actualizacion in-place usa ${ARTIFACT_NAME}_Mac_v<version>.zip (lo arma deploy.sh --zip)."
    echo ""
    echo "  --no-open   No revelar el DMG en Finder al terminar"
}

OPEN_AFTER=true
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-open) OPEN_AFTER=false; shift ;;
        -h|--help) show_help; exit 0 ;;
        *) echo "Opcion desconocida: $1"; show_help; exit 1 ;;
    esac
done

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "ERROR: falta el comando requerido: $1"
        exit 1
    fi
}

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "ERROR: create_dmg.sh debe ejecutarse en macOS."
    exit 1
fi

require_cmd hdiutil
require_cmd python3
require_cmd ditto

# La version sale del archivo VERSION si existe y, si no, de CMakeLists.txt: no todas las
# apps LGA tienen un archivo VERSION (VideoDownloader, por ejemplo, declara el numero solo en
# `project(... VERSION x.y ...)`), y hardcodear una de las dos formas deja el script inservible
# en la mitad de los repos.
if [[ -f "VERSION" ]]; then
    APP_VERSION="$(tr -d '\r\n' < VERSION)"
elif [[ -f "CMakeLists.txt" ]]; then
    APP_VERSION="$(sed -n 's/^[[:space:]]*project(.*VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
fi

if [[ -z "${APP_VERSION:-}" ]]; then
    echo "ERROR: no se pudo determinar la version (ni VERSION ni project(... VERSION ...) en CMakeLists.txt)."
    exit 1
fi
if ! [[ "$APP_VERSION" =~ ^[0-9]+(\.[0-9]+)+$ ]]; then
    echo "ERROR: VERSION tiene un formato invalido: $APP_VERSION"
    exit 1
fi

DMG_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.dmg"
VOL_NAME="${DISPLAY_NAME} ${APP_VERSION}"
SRC_APP="deploy/${APP_NAME}.app"
DMG_PATH="deploy/${DMG_NAME}"

# Geometria de la ventana, en PUNTOS. Tiene que coincidir con la de
# tools/macos/make_dmg_background.js, que dibuja el fondo contra estas mismas medidas.
#
# TRAMPA: la altura que se guarda en el `.DS_Store` incluye la barra de titulo y la de
# estado, pero el fondo y las posiciones de los iconos se miden desde el AREA DE
# CONTENIDO. Sin sumar ese chrome, lo ultimo del layout queda cortado abajo.
# Finder IGNORA el tamano de ventana guardado en el .DS_Store —esta escrito ahi y lo saltea
# igual, medido en macOS 26— y abre con el suyo, que deja unos 377 pt de contenido. TODO el
# layout tiene que entrar en esa altura. Eso es lo que fija el tamano de los iconos: con 128
# pt la segunda fila (el LEEME.txt debajo de la app) queda cortada y el usuario no la ve.
#
# Las dos columnas se definen como CX -/+ COL_DX para que la flecha del fondo, que va
# centrada en CX, quede con el mismo aire contra cada icono por construccion.
WIN_W=620
WIN_H=420          # area de contenido
CHROME_H=58        # barra de titulo + barra de estado
CX=$((WIN_W / 2))
COL_DX=125
ICON_SIZE=96
TEXT_SIZE=13
APP_X=$((CX - COL_DX)); APP_Y=132
APPS_X=$((CX + COL_DX)); APPS_Y=132
README_X=$((CX - COL_DX)); README_Y=268   # DEBAJO de la app, misma columna
# Todo lo que empieza con punto se manda fuera de la vista inicial: ademas del .background
# estan .fseventsd y .Trashes, que los crea el sistema al montar, y con "mostrar archivos
# ocultos" activado aparecen en el medio del layout.
HIDDEN_X=1400; HIDDEN_Y=1400

BACKGROUND_SRC="resources/dmg/dmg_background.tiff"

if [[ ! -d "$SRC_APP" ]]; then
    echo "ERROR: no existe $SRC_APP. Ejecuta ./deploy.sh antes de crear el DMG."
    exit 1
fi

# Un .app que ya se ejecuto desde deploy/ tiene la firma rota: el DMG saldria con un bundle que
# no verifica. Ver tools/macos/verify_bundle_signature.sh.
bash "./tools/macos/verify_bundle_signature.sh" "$SRC_APP"

# El fondo va VERSIONADO y no se genera en cada deploy: no cambia salvo que uno toque el
# diseno o el nombre de la app. Lo genera tools/macos/make_dmg_background.js, que dibuja con
# AppKit via JXA y NO necesita nada instalado. Ahi estan documentadas las restricciones de
# Finder que explican el diseno.
if [[ ! -f "$BACKGROUND_SRC" ]]; then
    echo "ERROR: falta $BACKGROUND_SRC."
    echo "Generalo con:"
    echo "  osascript -l JavaScript tools/macos/make_dmg_background.js \\"
    echo "      \"$BACKGROUND_SRC\" \"$DISPLAY_NAME\" \"Drag to install into your Applications folder\" \"$APP_NAME\""
    exit 1
fi

# --- dmgbuild VENDORIZADO ---------------------------------------------------------------
# `dmgbuild` y sus dos dependencias viven versionadas en tools/macos/vendor/. NO se instala
# nada: ni venv, ni pip, ni red, ni Python global ensuciado. Son 268 KB de Python puro —cero
# binarios compilados— y corren con el `python3` que ya trae macOS (probado en 3.9.6).
#
# Va vendorizado y no compartido por path relativo con las otras apps LGA: a ese peso, la
# copia sale gratis, y un `../LGA_Base_QT_C_Py/tools/...` se rompe apenas alguien clona una
# app sola. Al derivar una app se copia esta carpeta tal cual.
VENDOR_DIR="$SCRIPT_DIR/tools/macos/vendor"
if [[ ! -d "$VENDOR_DIR/dmgbuild" ]]; then
    echo "ERROR: falta $VENDOR_DIR/dmgbuild."
    echo "Regeneralo con: tools/macos/refresh_dmg_vendor.sh"
    exit 1
fi
# --------------------------------------------------------------------------------------

WORK_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/${ARTIFACT_NAME}_DMG_XXXXXX")"
MOUNT_DIR=""
cleanup() {
    if [[ -n "$MOUNT_DIR" ]] && mount | grep -Fq "$MOUNT_DIR"; then
        hdiutil detach "$MOUNT_DIR" -quiet || hdiutil detach "$MOUNT_DIR" -force -quiet || true
    fi
    rm -rf "$WORK_ROOT"
}
trap cleanup EXIT

cat > "$WORK_ROOT/LEEME.txt" << EOF
${DISPLAY_NAME} para macOS
=========================

Instalacion:
1. Arrastrar ${APP_NAME}.app a Applications.
2. Abrir la app desde Applications.

Si se muestra un aviso que impide abrir la app, hay que abrir la app "Terminal" y ejecutar:

sudo xattr -cr "/Applications/${APP_NAME}.app"

Luego abrir la app otra vez desde Applications.

Que hace este comando:
Quita el atributo de cuarentena del bundle instalado. No otorga permisos permanentes,
no cambia tus datos y no desactiva Gatekeeper globalmente. Solo limpia esa marca para
esa copia instalada.
EOF

cat > "$WORK_ROOT/settings.py" << EOF
import os

app = defines["app"]
app_name = os.path.basename(app)

# ditto y no cp: el .app esta lleno de symlinks (Versions/Current, el binario de cada
# framework) y hay que conservarlos, ademas de la firma. dmgbuild usa ditto internamente.
files = [app, defines["readme"]]
symlinks = {"Applications": "/Applications"}
background = defines["background"]

window_rect = ((160, 160), (${WIN_W}, $((WIN_H + CHROME_H))))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
arrange_by = None
icon_size = ${ICON_SIZE}
text_size = ${TEXT_SIZE}
label_pos = "bottom"

icon_locations = {
    app_name: (${APP_X}, ${APP_Y}),
    "Applications": (${APPS_X}, ${APPS_Y}),
    "LEEME.txt": (${README_X}, ${README_Y}),
    ".background.tiff": (${HIDDEN_X}, ${HIDDEN_Y}),
    ".fseventsd": (${HIDDEN_X}, $((HIDDEN_Y + 120))),
    ".Trashes": (${HIDDEN_X}, $((HIDDEN_Y + 240))),
}
EOF

echo "Armando el DMG ($DMG_NAME)..."
rm -f "$DMG_PATH"
mkdir -p deploy
PYTHONPATH="$VENDOR_DIR" python3 -m dmgbuild -s "$WORK_ROOT/settings.py" \
    -D app="$PWD/$SRC_APP" \
    -D readme="$WORK_ROOT/LEEME.txt" \
    -D background="$PWD/$BACKGROUND_SRC" \
    "$VOL_NAME" "$DMG_PATH"

# --- Verificacion ---------------------------------------------------------------------
# Sobre el ARCHIVO producido, no sobre lo que muestre Finder: montado en la maquina que lo
# construyo, Finder puede pintar la ventana con su estado cacheado de la sesion y hacer
# pasar por bueno un DMG al que le falta el fondo. Esto ya paso.
echo "Verificando el layout del DMG..."
MOUNT_DIR="$(hdiutil attach "$DMG_PATH" -readonly -nobrowse -noautoopen | awk '/\/Volumes\// {for (i=3; i<=NF; i++) printf "%s%s", (i==3 ? "" : " "), $i; print ""}' | tail -n 1)"
if [[ -z "$MOUNT_DIR" || ! -d "$MOUNT_DIR" ]]; then
    echo "ERROR: no se pudo montar el DMG para verificarlo."
    exit 1
fi

VERIFY_FAILED=false
[[ -d "$MOUNT_DIR/${APP_NAME}.app" ]] || { echo "ERROR: el DMG no tiene ${APP_NAME}.app"; VERIFY_FAILED=true; }
[[ -L "$MOUNT_DIR/Applications" ]]    || { echo "ERROR: el DMG no tiene el alias a Applications"; VERIFY_FAILED=true; }
[[ -f "$MOUNT_DIR/.DS_Store" ]]       || { echo "ERROR: el DMG no tiene .DS_Store (no hay layout)"; VERIFY_FAILED=true; }

# La prueba de que el fondo quedo REALMENTE seteado: la clave `backgroundImageAlias` en el
# `.DS_Store`. Un layout sin fondo trae SOLO las claves `backgroundColor*` —asi salia el DMG
# roto— y esas estan siempre, con fondo o sin el, asi que buscarlas a ellas no prueba nada.
if ! strings "$MOUNT_DIR/.DS_Store" | grep -q "backgroundImageAlias"; then
    echo "ERROR: el .DS_Store no tiene backgroundImageAlias — el DMG quedaria con fondo blanco."
    VERIFY_FAILED=true
fi
if [[ ! -f "$MOUNT_DIR/.background.tiff" ]]; then
    echo "ERROR: el DMG no contiene .background.tiff"
    VERIFY_FAILED=true
fi

hdiutil detach "$MOUNT_DIR" -quiet
MOUNT_DIR=""

if [[ "$VERIFY_FAILED" == "true" ]]; then
    rm -f "$DMG_PATH"
    echo "DMG descartado por fallar la verificacion."
    exit 1
fi
# --------------------------------------------------------------------------------------

echo "DMG creado: $DMG_PATH"
echo "OK: layout verificado sobre el archivo (fondo, alias y .DS_Store presentes)."

if [[ "$OPEN_AFTER" == "true" ]]; then
    open -R "$DMG_PATH"
fi

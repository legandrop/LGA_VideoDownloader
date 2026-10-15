#!/bin/bash

set -euo pipefail

# Instala un .app en /Applications y lo abre. Es el paso final de todo `deploy.sh` de macOS.
#
# Pieza CANONICA de LGA_Base_QT_C_Py: cada app la copia IDENTICA a su `tools/macos/`. No se
# modifica en la app: el cambio se hace aca y se vuelve a copiar a todas. Contrato completo en
# docs/Doc_Deploy_macOS.md, seccion "El final del deploy: instalar y abrir".
#
# Por que la app se abre INSTALADA y nunca desde deploy/:
#   - El bundle de deploy/ es el que se empaqueta y se publica. Ejecutarlo lo altera (el Python
#     embebido escribe __pycache__ y logs adentro) y rompe la firma que verifica el updater: asi
#     salieron rotos los zips de macOS de PipeSync de v1.000 a v1.018.
#   - Abierta con `open` desde /Applications, macOS le atribuye los permisos a la app y no a la
#     terminal, y es la misma ruta que tiene la app del usuario: asociaciones de archivos, URL
#     schemes, inicio con la sesion.
#   - Con `open` la salida de la app no cae en la terminal y no tapa las preguntas que siguen.

show_help() {
    echo "Uso: $0 <App.app> [--no-open] [--wait] [--dest <carpeta>]"
    echo ""
    echo "  --no-open   Instala sin abrir la app"
    echo "  --wait      Abre el binario instalado en primer plano: se ven su stdout/stderr y su"
    echo "              exit code, y la terminal queda retenida hasta que se cierre la app"
    echo "  --dest      Carpeta destino (por defecto /Applications). Sirve para probar sin tocar"
    echo "              la copia instalada"
}

SRC=""
DEST_DIR="/Applications"
OPEN_AFTER=true
WAIT=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-open) OPEN_AFTER=false; shift ;;
        --wait) WAIT=true; shift ;;
        --dest)
            [[ $# -ge 2 ]] || { echo "ERROR: --dest requiere una carpeta."; exit 1; }
            DEST_DIR="$2"
            shift 2
            ;;
        -h|--help) show_help; exit 0 ;;
        -*) echo "Opcion desconocida: $1"; show_help; exit 1 ;;
        *)
            [[ -z "$SRC" ]] || { echo "ERROR: un solo .app por llamada."; exit 1; }
            SRC="$1"
            shift
            ;;
    esac
done

if [[ -z "$SRC" || "$SRC" != *.app || ! -d "$SRC" ]]; then
    echo "ERROR: no existe el bundle '${SRC}'."
    show_help
    exit 1
fi
if [[ ! -d "$DEST_DIR" ]]; then
    echo "ERROR: no existe la carpeta destino $DEST_DIR"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_BUNDLE="$(basename "$SRC")"
SRC_ABS="$(cd "$(dirname "$SRC")" && pwd)/${APP_BUNDLE}"
DEST_ABS="$(cd "$DEST_DIR" && pwd)"
TARGET="${DEST_ABS}/${APP_BUNDLE}"

if [[ "$SRC_ABS" == "$TARGET" ]]; then
    echo "ERROR: el origen y el destino son el mismo bundle: $TARGET"
    exit 1
fi

EXECUTABLE="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "${SRC_ABS}/Contents/Info.plist" 2>/dev/null || true)"
if [[ -z "$EXECUTABLE" || ! -x "${SRC_ABS}/Contents/MacOS/${EXECUTABLE}" ]]; then
    echo "ERROR: ${APP_BUNDLE} no declara un CFBundleExecutable valido."
    exit 1
fi

# Todo lo que corre DESDE la copia instalada: la app y sus auxiliares (Redis, el Python
# embebido, helpers), que viven adentro del bundle. Por RUTA y nunca por nombre: no se toca la
# copia de build/ ni la de deploy/, ni otra app con el mismo nombre de proceso.
running_from_target() {
    pgrep -f "${TARGET}/Contents/" >/dev/null 2>&1
}

if running_from_target; then
    echo "Cerrando la copia instalada que esta corriendo..."
    # SIGTERM al binario principal primero, y unos segundos para que baje sus auxiliares. No
    # se usa `osascript ... to quit`: mandarle un Apple event a otra app dispara el permiso de
    # Automatizacion de macOS, que pregunta en un cartel y deja el script colgado esperando.
    pkill -TERM -f "${TARGET}/Contents/MacOS/${EXECUTABLE}" 2>/dev/null || true
    for _ in $(seq 1 20); do
        running_from_target || break
        sleep 0.5
    done
    if running_from_target; then
        pkill -TERM -f "${TARGET}/Contents/" 2>/dev/null || true
        sleep 2
    fi
    if running_from_target; then
        pkill -KILL -f "${TARGET}/Contents/" 2>/dev/null || true
        sleep 1
    fi
    if running_from_target; then
        echo "ERROR: no se pudo cerrar la copia instalada en $TARGET"
        exit 1
    fi
fi

echo "Instalando ${APP_BUNDLE} en ${DEST_ABS}..."
rm -rf "$TARGET"
# ditto y no `cp -R`: conserva symlinks, permisos y metadata del bundle tal cual, y con eso
# la firma.
ditto "$SRC_ABS" "$TARGET"
# Que LaunchServices tome la copia nueva (icono, tipos de documento, URL schemes): el bundle id
# es el mismo de siempre y sin esto puede seguir mostrando lo de la version anterior.
touch "$TARGET"
LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
if [[ -x "$LSREGISTER" ]]; then
    "$LSREGISTER" -f "$TARGET" >/dev/null 2>&1 || true
fi

if [[ -f "${SCRIPT_DIR}/verify_bundle_signature.sh" ]]; then
    bash "${SCRIPT_DIR}/verify_bundle_signature.sh" "$TARGET"
fi
echo "Instalada: $TARGET"

if [[ "$WAIT" == "true" ]]; then
    echo "Abriendo en primer plano (--wait); la terminal queda retenida hasta cerrar la app..."
    "${TARGET}/Contents/MacOS/${EXECUTABLE}"
elif [[ "$OPEN_AFTER" == "true" ]]; then
    # -n: abre ESTA copia aunque haya otra con el mismo bundle id corriendo (la de build/, por
    # ejemplo). Sin -n, LaunchServices puede activar la que ya corre y no abrir la instalada.
    open -n "$TARGET"
    echo "Abierta."
fi

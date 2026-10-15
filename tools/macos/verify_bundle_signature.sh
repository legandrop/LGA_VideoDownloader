#!/bin/bash

set -euo pipefail

# Verifica la firma de un .app —o del .app que trae un .zip— con el MISMO comando que corre el
# helper de auto-update de macOS de las apps LGA (`codesign --verify --deep --strict`). Si esto
# falla, el helper hace rollback y reabre la version vieja en TODAS las Macs: el update no se
# completa nunca y el usuario no se entera de por que.
#
# Pieza CANONICA de LGA_Base_QT_C_Py: cada app la copia IDENTICA a su `tools/macos/`. No se
# modifica en la app: el cambio se hace aca y se vuelve a copiar a todas. Donde se llama, en
# docs/Doc_Deploy_macOS.md.
#
# Con un .zip se extrae con `ditto` a una carpeta temporal, igual que el helper: lo que se
# verifica es lo que le va a llegar al usuario, no el .app del que salio el zip.
#
# La causa conocida es ejecutar la app desde el bundle DESPUES de firmarlo: el Python embebido
# escribe `__pycache__/*.pyc` (y a veces logs) adentro de Contents/Resources y la firma queda
# invalida ("a sealed resource is missing or invalid"). Asi salieron rotos los zips de macOS de
# PipeSync de v1.000 a v1.018.

show_help() {
    echo "Uso: $0 <App.app | App.zip>"
    echo ""
    echo "Verifica con 'codesign --verify --deep --strict' que la firma del bundle este intacta."
    echo "Con un .zip, lo extrae con ditto y verifica el .app de su raiz."
}

if [[ $# -ne 1 ]]; then
    show_help
    exit 1
fi

TARGET="$1"
APP_PATH="$TARGET"

if [[ "$TARGET" == *.zip ]]; then
    if [[ ! -f "$TARGET" ]]; then
        echo "ERROR: no existe el zip $TARGET"
        exit 1
    fi
    WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/verify_bundle_signature_XXXXXX")"
    trap 'rm -rf "$WORK_DIR"' EXIT
    /usr/bin/ditto -x -k "$TARGET" "$WORK_DIR"
    APP_PATH="$(/usr/bin/find "$WORK_DIR" -maxdepth 1 -name '*.app' -print -quit)"
    if [[ -z "$APP_PATH" ]]; then
        echo "ERROR: el zip no trae un .app en su raiz: $TARGET"
        exit 1
    fi
elif [[ ! -d "$TARGET" ]]; then
    echo "ERROR: no existe el bundle $TARGET"
    exit 1
fi

if ! verify_output="$(/usr/bin/codesign --verify --deep --strict -vvvv "$APP_PATH" 2>&1)"; then
    echo "ERROR: la firma de $(basename "$APP_PATH") no es valida ($TARGET)."
    echo "       El auto-update de macOS lo rechazaria y haria rollback en todas las Macs."
    # Here-string y no `echo |`: con `head` cortando, el echo moria con "Broken pipe".
    grep -v -e '^--prepared:' -e '^--validated:' <<< "$verify_output" | head -15 | sed 's/^/       /' || true
    if [[ "$verify_output" == *__pycache__* ]]; then
        echo "       Hay .pyc que no estaban al firmar: la app corrio desde este bundle despues"
        echo "       de firmarlo. Regenerar el deploy sin lanzar la app antes de empaquetar."
    fi
    exit 1
fi

echo "Firma OK: $TARGET"

#!/bin/bash
set -euo pipefail

# Notariza un .app o un .dmg firmado con Developer ID, le abrocha el ticket ("staple") y
# comprueba que Gatekeeper lo acepte como lo veria otra Mac.
# Pieza canonica de LGA_Base_QT_C_Py: cada app la copia IDENTICA y no la edita.
# Detalle: docs/Doc_Firma_Apple.md §5.
#
# Uso: notarize.sh <App.app | archivo.dmg>
#
# La credencial NO pasa por aca: notarytool la lee del llavero por el nombre del perfil
# (LGA_NOTARY_PROFILE, por defecto LGA-notary), que carga Lega a mano una sola vez.
#
# Salida: 0 notarizado y abrochado; 1 Apple lo rechazo o fallo algo (imprime el log de Apple);
#         2 no se puede notarizar aca (no es Developer ID o falta el perfil): el que llama decide.

PROFILE="${LGA_NOTARY_PROFILE:-LGA-notary}"
TARGET="${1:-}"
[[ $# -eq 1 && -n "$TARGET" ]] || { echo "Uso: $0 <App.app | archivo.dmg>" >&2; exit 1; }
TARGET="${TARGET%/}"
[[ -e "$TARGET" ]] || { echo "ERROR: no existe $TARGET" >&2; exit 1; }

case "$TARGET" in
    *.app|*.dmg) ;;
    *) echo "ERROR: solo notariza .app o .dmg: $TARGET" >&2; exit 1 ;;
esac

# Se captura antes de buscar: con pipefail, `codesign | grep -q` da falso aunque encuentre la linea
# (grep corta la tuberia y codesign muere por SIGPIPE).
SIG_INFO="$(/usr/bin/codesign -dv --verbose=2 "$TARGET" 2>&1 || true)"
if [[ "$SIG_INFO" != *$'\n'"Authority=Developer ID Application:"* ]]; then
    echo "Sin notarizar: $(basename "$TARGET") no esta firmado con Developer ID."
    exit 2
fi
if ! xcrun notarytool history --keychain-profile "$PROFILE" >/dev/null 2>&1; then
    echo "Sin notarizar: no hay credencial de notarizacion en el llavero (perfil '$PROFILE')."
    echo "  Se carga una vez, a mano: docs/Doc_Firma_Apple.md §4 de LGA_Base_QT_C_Py."
    exit 2
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/lga_notarize_XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# Un .app se sube como zip (ditto, que conserva symlinks y la firma); un .dmg, tal cual.
UPLOAD="$TARGET"
if [[ "$TARGET" == *.app ]]; then
    UPLOAD="$WORK/$(basename "$TARGET" .app).zip"
    ditto -c -k --sequesterRsrc --keepParent "$TARGET" "$UPLOAD"
fi

echo "Notarizando $(basename "$TARGET") con Apple (suele tardar de 1 a 10 minutos)..."
# 3 reintentos cada 5 s cuando el envio no llega a Apple (regla de red de LGA). Si Apple ya le
# dio un id al envio, no se reintenta: se espera ese resultado.
OUT=""
SUB_ID=""
for attempt in 1 2 3 4; do
    if OUT="$(xcrun notarytool submit "$UPLOAD" --keychain-profile "$PROFILE" --wait --timeout 30m --output-format plist 2>"$WORK/err.txt")"; then
        :
    fi
    printf '%s' "$OUT" > "$WORK/out.plist"
    SUB_ID="$(/usr/libexec/PlistBuddy -c 'Print :id' "$WORK/out.plist" 2>/dev/null || true)"
    [[ -n "$SUB_ID" ]] && break
    if [[ $attempt -lt 4 ]]; then
        echo "   AVISO: el envio no llego a Apple (intento $attempt); reintento en 5 s..."
        sed 's/^/   /' "$WORK/err.txt" | tail -3
        sleep 5
    fi
done
if [[ -z "$SUB_ID" ]]; then
    echo "ERROR: no se pudo enviar a notarizar:"
    cat "$WORK/err.txt"
    exit 1
fi
STATUS="$(/usr/libexec/PlistBuddy -c 'Print :status' "$WORK/out.plist" 2>/dev/null || echo "?")"
echo "   Envio $SUB_ID: $STATUS"

if [[ "$STATUS" != "Accepted" ]]; then
    echo "ERROR: Apple no lo acepto. Lo que dice el informe:"
    xcrun notarytool log "$SUB_ID" --keychain-profile "$PROFILE" 2>&1 | sed 's/^/   /' | head -60
    exit 1
fi

# El ticket abrochado deja que Gatekeeper lo acepte sin conexion. stapler a veces no encuentra el
# ticket recien emitido: se reintenta.
for attempt in 1 2 3 4; do
    if xcrun stapler staple -q "$TARGET" >/dev/null 2>&1; then
        break
    fi
    if [[ $attempt -eq 4 ]]; then
        echo "ERROR: Apple acepto el envio pero no se pudo abrochar el ticket a $(basename "$TARGET")."
        exit 1
    fi
    sleep 5
done
xcrun stapler validate -q "$TARGET"

# Como lo veria otra Mac: "source=Notarized Developer ID".
if [[ "$TARGET" == *.app ]]; then
    ASSESS="$(spctl -a -t exec -vv "$TARGET" 2>&1 || true)"
else
    ASSESS="$(spctl -a -t open --context context:primary-signature -vv "$TARGET" 2>&1 || true)"
fi
if [[ "$ASSESS" != *"source=Notarized Developer ID"* ]]; then
    echo "ERROR: Gatekeeper no lo da por notarizado:"
    printf '%s\n' "$ASSESS" | sed 's/^/   /'
    exit 1
fi
echo "OK: $(basename "$TARGET") notarizado y abrochado (Gatekeeper: Notarized Developer ID)."

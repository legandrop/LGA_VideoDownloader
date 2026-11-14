#!/bin/bash
set -euo pipefail

# Firma un .app (o un .dmg) de una app LGA con la mejor identidad que haya en el llavero.
# Pieza canonica de LGA_Base_QT_C_Py: cada app la copia IDENTICA y no la edita.
# Detalle: docs/Doc_Firma_Apple.md y docs/Doc_Deploy_macOS.md §2.
#
# Uso:
#   sign_bundle.sh <App.app> [--entitlements <plist>]
#   sign_bundle.sh <archivo.dmg>
#   sign_bundle.sh --kind        imprime developer-id | stable | adhoc y sale
#   sign_bundle.sh --identity    imprime la identidad elegida y sale
#
# Orden de eleccion (lo dice en la salida):
#   1. LGA_CODESIGN_IDENTITY, si esta definida.
#   2. "Developer ID Application: ... (<LGA_TEAM_ID>)", Team por defecto YV64L3LUMB.
#   3. "LGA Code Signing", la casera de crear_identidad_firma.sh.
#   4. ad-hoc ("-").
#
# Con Developer ID se firma como pide la notarizacion de Apple: hardened runtime, sello de
# tiempo y de ADENTRO hacia AFUERA (cada Mach-O suelto, despues cada .framework, despues los
# bundles anidados y al final el .app), sin --deep. Con las otras dos identidades se firma como
# antes (--deep, sin runtime), porque no se notariza y asi no cambia nada en las apps que todavia
# no migraron.
#
# Nunca recibe ni escribe una credencial: la clave vive en el llavero.

TEAM_ID="${LGA_TEAM_ID:-YV64L3LUMB}"
STABLE_NAME="LGA Code Signing"

pick_identity() {
    # Deja IDENTITY (hash o nombre) , IDENTITY_LABEL y KIND.
    local ids line
    ids="$(security find-identity -v -p codesigning 2>/dev/null || true)"
    if [[ -n "${LGA_CODESIGN_IDENTITY:-}" ]]; then
        IDENTITY="$LGA_CODESIGN_IDENTITY"
        IDENTITY_LABEL="$LGA_CODESIGN_IDENTITY (forzada por LGA_CODESIGN_IDENTITY)"
        if [[ "$IDENTITY" == "-" ]]; then
            KIND="adhoc"
        elif [[ "$IDENTITY" == Developer\ ID\ Application:* ]]; then
            KIND="developer-id"
        else
            KIND="stable"
        fi
        return
    fi
    line="$(printf '%s\n' "$ids" | grep -E "\"Developer ID Application: .*\(${TEAM_ID}\)\"" | head -1 || true)"
    if [[ -n "$line" ]]; then
        # El hash y no el nombre: con dos certificados vigentes del mismo nombre (renovacion),
        # codesign corta por ambiguo. El primero de la lista es el que se usa.
        IDENTITY="$(printf '%s' "$line" | awk '{print $2}')"
        IDENTITY_LABEL="$(printf '%s' "$line" | sed -E 's/^[^"]*"(.*)"$/\1/')"
        KIND="developer-id"
        return
    fi
    if [[ "$ids" == *"\"${STABLE_NAME}\""* ]]; then
        IDENTITY="$STABLE_NAME"
        IDENTITY_LABEL="$STABLE_NAME"
        KIND="stable"
        return
    fi
    IDENTITY="-"
    IDENTITY_LABEL="ad-hoc"
    KIND="adhoc"
}

# codesign con 3 reintentos cada 5 s si falla el sello de tiempo (regla de red de LGA): el
# servidor de Apple a veces no contesta, y en la PC de Lega el firewall corta el primer intento.
cs() {
    local out attempt
    for attempt in 1 2 3 4; do
        if out="$(/usr/bin/codesign "$@" 2>&1)"; then
            return 0
        fi
        if [[ "$out" == *"timestamp"* || "$out" == *"network"* || "$out" == *"connection"* ]] && [[ $attempt -lt 4 ]]; then
            echo "   AVISO: fallo el sello de tiempo (intento $attempt); reintento en 5 s..." >&2
            sleep 5
            continue
        fi
        printf '%s\n' "$out" >&2
        return 1
    done
}

is_macho() {
    # Sin `| grep -q`: con pipefail, grep corta la tuberia y el resultado puede salir falso.
    [[ "$(/usr/bin/file -b "$1" 2>/dev/null)" == *"Mach-O"* ]]
}

# Lista (una por linea) ordenada de la ruta mas profunda a la menos profunda.
deepest_first() {
    awk -F/ '{print NF "\t" $0}' | sort -t$'\t' -k1,1nr -k2 | cut -f2-
}

sign_app_developer_id() {
    local app="$1" ent="$2" main_exe f count=0
    main_exe="$app/Contents/MacOS/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$app/Contents/Info.plist")"
    local common=(--force --timestamp --options runtime --sign "$IDENTITY")

    # 1. Cada Mach-O suelto (dylibs, plugins de Qt, binarios de frameworks, ejecutables
    #    auxiliares, extensiones de Python), menos el ejecutable principal.
    local machos=()
    while IFS= read -r f; do
        [[ "$f" == "$main_exe" ]] && continue
        is_macho "$f" && machos+=("$f")
    done < <(find "$app" -type f ! -path "*/_CodeSignature/*" | deepest_first)
    for f in ${machos[@]+"${machos[@]}"}; do
        cs "${common[@]}" "$f"
        count=$((count + 1))
    done

    # 2. Cada .framework como bundle (sella su Info.plist y Resources), y 3. los bundles
    #    anidados (.app, .appex, .xpc, .bundle con ejecutable), siempre de adentro hacia afuera.
    while IFS= read -r f; do
        cs "${common[@]}" "$f"
        count=$((count + 1))
    done < <(find "$app/Contents" -type d \( -name '*.framework' -o -name '*.app' -o -name '*.appex' -o -name '*.xpc' \) | deepest_first)

    # 4. El .app, con sus entitlements si los tiene.
    if [[ -n "$ent" ]]; then
        cs "${common[@]}" --entitlements "$ent" "$app"
    else
        cs "${common[@]}" "$app"
    fi
    echo "   ${count} piezas internas firmadas, mas el .app."
}

# ---- Argumentos -----------------------------------------------------------------------------
TARGET=""
ENTITLEMENTS=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --kind) pick_identity; echo "$KIND"; exit 0 ;;
        --identity) pick_identity; echo "$IDENTITY_LABEL"; exit 0 ;;
        --entitlements)
            [[ $# -ge 2 ]] || { echo "ERROR: --entitlements requiere un .plist." >&2; exit 1; }
            ENTITLEMENTS="$2"; shift 2 ;;
        -h|--help) sed -n '4,26p' "$0"; exit 0 ;;
        -*) echo "ERROR: opcion desconocida: $1" >&2; exit 1 ;;
        *)
            [[ -z "$TARGET" ]] || { echo "ERROR: un solo destino por llamada." >&2; exit 1; }
            TARGET="$1"; shift ;;
    esac
done
[[ -n "$TARGET" ]] || { echo "ERROR: falta el .app o el .dmg a firmar." >&2; exit 1; }
[[ -e "$TARGET" ]] || { echo "ERROR: no existe $TARGET" >&2; exit 1; }
if [[ -n "$ENTITLEMENTS" && ! -f "$ENTITLEMENTS" ]]; then
    echo "ERROR: no existe el archivo de entitlements $ENTITLEMENTS" >&2
    exit 1
fi

pick_identity

case "$TARGET" in
    *.dmg)
        # El DMG solo se firma con Developer ID: es lo que se notariza. Con las otras
        # identidades no se firmaba antes y no se empieza ahora.
        if [[ "$KIND" != "developer-id" ]]; then
            echo "DMG sin firmar (la identidad es $IDENTITY_LABEL, no Developer ID)."
            exit 0
        fi
        echo "Firmando el DMG con '$IDENTITY_LABEL'..."
        cs --force --timestamp --sign "$IDENTITY" "$TARGET"
        /usr/bin/codesign --verify --strict "$TARGET"
        ;;
    *.app|*.app/)
        TARGET="${TARGET%/}"
        case "$KIND" in
            developer-id)
                echo "Firmando el bundle con '$IDENTITY_LABEL' (hardened runtime, sello de tiempo)..."
                sign_app_developer_id "$TARGET" "$ENTITLEMENTS"
                ;;
            stable)
                echo "Firmando el bundle con '$IDENTITY_LABEL' (sin notarizar: los DMG siguen pidiendo xattr)..."
                cs --force --deep --sign "$IDENTITY" "$TARGET"
                ;;
            adhoc)
                echo "Firmando el bundle (ad-hoc: no hay Developer ID ni '$STABLE_NAME' en el llavero)..."
                cs --force --deep --sign - "$TARGET"
                ;;
        esac
        ;;
    *)
        echo "ERROR: solo firma .app o .dmg: $TARGET" >&2
        exit 1
        ;;
esac

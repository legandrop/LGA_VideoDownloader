#!/bin/bash
set -euo pipefail

# Deploy LOCAL de macOS: compila en Release, arma y verifica el bundle igual que ./deploy.sh,
# lo instala en /Applications y abre la copia instalada. Arma SIEMPRE el DMG de cada edicion,
# para tener a mano lo que se le manda a alguien (regla de Lega, 2026-10-26). Sin ZIP y sin
# publicar en GitHub. Acepta cambios sin commitear.
#
# Es el cierre de toda implementacion en la Mac: lo que se prueba es la app instalada, no la de
# build/. Detalle en docs/Doc_Deploy_macOS.md §6 de LGA_Base_QT_C_Py.
#
# No duplica el deploy: delega en el deploy de cada edicion, que es el unico dueno del build y de
# la verificacion del bundle. --no-publish apaga la publicacion, --dmg arma el DMG sin preguntar,
# y stdin en /dev/null apaga las demas preguntas (ZIP, Finder), que los deploy hacen solo con
# terminal. No se pasa --no-open-finder porque no todos los deploy.sh lo conocen y cortan ante una
# opcion desconocida. La pregunta de Finder la hace esta pieza una sola vez, al final, por los DMG
# de todas las ediciones.
#
# Pieza canonica de LGA_Base_QT_C_Py: cada app la copia y solo cambia EDITIONS. Una app con
# varias ediciones (Studio y Client) las lista todas: el deploy local las instala SIEMPRE todas,
# porque una edicion que no se instala queda vieja sin que nadie lo note.

# "nombre:comando". El comando corre desde la raiz del repo.
EDITIONS=("app:./deploy.sh")

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

show_help() {
    local names=""
    for entry in "${EDITIONS[@]}"; do names+="${entry%%:*} "; done
    echo "Uso: $0 [--only <edicion>] [--no-open] [--no-install] [--parallel N]"
    echo ""
    echo "  Compila en Release, arma el DMG, instala en /Applications y abre la app."
    echo "  Sin ZIP y sin publicar en GitHub. Ediciones: ${names% }"
    echo ""
    echo "  --only <edicion>  Instalar solo esa edicion"
    echo "  --no-open         Instalar en /Applications sin abrir la app"
    echo "  --no-install      Solo compilar y verificar el bundle de deploy/, sin instalar"
    echo "  --parallel N      Usar N nucleos para compilar (si el deploy.sh lo admite)"
}

ONLY=""
PASS_ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) show_help; exit 0 ;;
        --only)
            [ $# -ge 2 ] || { echo "ERROR: --only requiere una edicion."; exit 1; }
            ONLY="$2"; shift 2 ;;
        --no-open|--no-install) PASS_ARGS+=("$1"); shift ;;
        --parallel)
            [ $# -ge 2 ] || { echo "ERROR: --parallel requiere un valor."; exit 1; }
            PASS_ARGS+=("$1" "$2"); shift 2 ;;
        --dmg) shift ;;  # ya es el default
        --zip|--publish|--no-publish|--dry-run|--replace|--yes)
            echo "ERROR: $1 no va en el deploy local; usar ./deploy.sh."
            exit 1 ;;
        *) echo "ERROR: opcion desconocida: $1"; show_help; exit 1 ;;
    esac
done

# Una tecla, sin Enter; una tecla que no es opcion no hace nada (regla LGA, igual que deploy.sh).
read_key() {  # <opciones validas, ej. "YN">
    local k
    while true; do
        IFS= read -rsn1 k || { KEY=""; return 1; }
        k="$(printf '%s' "$k" | tr '[:lower:]' '[:upper:]')"
        if [[ -n "$k" && "$1" == *"$k"* ]]; then
            KEY="$k"
            echo "$k"
            return 0
        fi
    done
}

# Los DMG de ESTA corrida se encuentran por fecha: la carpeta la decide el deploy.sh de cada
# edicion (deploy/, deploy_client/...) y esta pieza no la conoce.
STAMP="$(mktemp "${TMPDIR:-/tmp}/deploy_local_XXXXXX")"
trap 'rm -f "$STAMP"' EXIT

RAN=0
for entry in "${EDITIONS[@]}"; do
    name="${entry%%:*}"
    [ -n "$ONLY" ] && [ "$ONLY" != "$name" ] && continue
    read -r -a cmd <<< "${entry#*:}"
    echo ""
    echo "===== Deploy local: ${name} (${cmd[*]}) ====="
    bash "${cmd[@]}" --no-publish --dmg ${PASS_ARGS[@]+"${PASS_ARGS[@]}"} < /dev/null
    RAN=$((RAN + 1))
done

if [ "$RAN" -eq 0 ]; then
    echo "ERROR: no hay ninguna edicion llamada '${ONLY}'."
    show_help
    exit 1
fi

DMGS=()
while IFS= read -r f; do DMGS+=("$f"); done < <(find . -maxdepth 2 -name '*.dmg' -newer "$STAMP" | sort)
if [ ${#DMGS[@]} -eq 0 ]; then
    echo "ERROR: el deploy no dejo ningun .dmg nuevo."
    exit 1
fi

echo ""
echo "DMG listos para mandar:"
for f in "${DMGS[@]}"; do echo "  ${ROOT_DIR}/${f#./}"; done

if [ -t 0 ]; then
    echo ""
    printf '%s [Y/N] ' "¿Queres abrir en Finder la ubicacion de los DMG?"
    if read_key "YN" && [ "$KEY" = "Y" ]; then
        open -R "${DMGS[@]}"
    fi
fi

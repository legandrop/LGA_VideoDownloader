#!/bin/bash
set -euo pipefail

# Deploy LOCAL de macOS: compila en Release, arma y verifica el bundle igual que ./deploy.sh,
# lo instala en /Applications y abre la copia instalada. Sin ZIP, sin DMG, sin publicar en
# GitHub y sin preguntar nada. Acepta cambios sin commitear.
#
# Es el cierre de toda implementacion en la Mac: lo que se prueba es la app instalada, no la de
# build/. Detalle en docs/Doc_Deploy_macOS.md §6 de LGA_Base_QT_C_Py.
#
# No duplica el deploy: delega en el deploy de cada edicion, que es el unico dueno del build y de
# la verificacion del bundle. --no-publish apaga la publicacion, y stdin en /dev/null apaga las
# preguntas (ZIP, DMG, Finder), que los deploy hacen solo con terminal. No se pasa
# --no-open-finder porque no todos los deploy.sh lo conocen y cortan ante una opcion desconocida.
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
    echo "  Compila en Release, instala en /Applications y abre la app."
    echo "  Sin ZIP, sin DMG y sin publicar en GitHub. Ediciones: ${names% }"
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
        --zip|--dmg|--publish|--no-publish|--dry-run|--replace|--yes)
            echo "ERROR: $1 no va en el deploy local; usar ./deploy.sh."
            exit 1 ;;
        *) echo "ERROR: opcion desconocida: $1"; show_help; exit 1 ;;
    esac
done

RAN=0
for entry in "${EDITIONS[@]}"; do
    name="${entry%%:*}"
    [ -n "$ONLY" ] && [ "$ONLY" != "$name" ] && continue
    read -r -a cmd <<< "${entry#*:}"
    echo ""
    echo "===== Deploy local: ${name} (${cmd[*]}) ====="
    bash "${cmd[@]}" --no-publish ${PASS_ARGS[@]+"${PASS_ARGS[@]}"} < /dev/null
    RAN=$((RAN + 1))
done

if [ "$RAN" -eq 0 ]; then
    echo "ERROR: no hay ninguna edicion llamada '${ONLY}'."
    show_help
    exit 1
fi

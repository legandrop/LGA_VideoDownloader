#!/bin/sh
# Sincroniza la version del repo.
# La logica esta en tools/sync_version.py; esto es solo el wrapper de Unix.
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SCRIPT="$DIR/tools/sync_version.py"

if [ ! -f "$SCRIPT" ]; then
    echo "ERROR: No se encontro \"$SCRIPT\"."
    exit 1
fi

# Orden de busqueda del interprete. Cada candidato se PRUEBA corriendolo: en
# Windows (Git Bash), "python" y "python3" existen en el PATH como alias del
# Microsoft Store, imprimen "Python was not found" y salen con error, asi que
# verificar que estan no alcanza.
PY=""
for c in "$DIR/python_runtime/windows/python.exe" \
         "$DIR/python_runtime/macos/python3/python3" \
         "$DIR/../LGA_FileManagerS3/python_runtime/windows/python.exe" \
         "$DIR/../LGA_FileManagerS3/python_runtime/macos/python3/python3"; do
    # En macOS se prueba `python_runtime/macos/python3/python3`, que es la ruta
    # que resuelve AppPathManager. OJO: ese `python3` es un SYMLINK a
    # `bin/python3.10`, y ese `bin/` no esta versionado en FileManagerS3, asi
    # que hoy el symlink queda COLGADO en cualquier clon nuevo y el candidato
    # falla. Esta igual porque el dia que se versione `bin/` empieza a andar
    # solo, sin tocar esto.
    #
    # El `-f` es el que salva: en un symlink colgado da falso y se pasa al
    # candidato siguiente. Y hace falta ademas del `-x` porque en un directorio
    # `-x` significa "se puede entrar" y da true.
    [ -f "$c" ] && [ -x "$c" ] || continue
    if "$c" -c "pass" >/dev/null 2>&1; then PY="$c"; break; fi
done
if [ -z "$PY" ]; then
    for c in "py -3" python3 python; do
        if $c -c "pass" >/dev/null 2>&1; then PY="$c"; break; fi
    done
fi

if [ -z "$PY" ]; then
    echo "ERROR: no encontre un interprete de Python usable."
    exit 1
fi

exec $PY "$SCRIPT" "$@"

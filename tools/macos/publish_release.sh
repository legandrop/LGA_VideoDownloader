#!/bin/bash

set -euo pipefail

# Publica el release de macOS de una app LGA en GitHub: el .zip (lo que baja el auto-update) y el
# .dmg (primera instalacion), mas SHA256SUMS y las notas para el usuario (What's new) si la app las
# usa. Lo llama el `deploy.sh` de cada app, en dos tiempos:
#
#   publish_release.sh --preflight [opciones]   ANTES de compilar: junta TODOS los problemas que
#                                               impedirian publicar y corta sin tocar nada.
#   publish_release.sh --expect-head <sha> [...] DESPUES de armar el .zip y el .dmg: revalida todo
#                                               (Windows pudo publicar en el medio) y publica.
#
# Pieza CANONICA de LGA_Base_QT_C_Py: cada app la copia IDENTICA a su `tools/macos/` y pone lo
# suyo en `tools/macos/release.conf` (variables, sin logica). No se modifica en la app: el cambio
# se hace aca y se vuelve a copiar a todas. Contrato y parametros: docs/Doc_Deploy_macOS.md §5.
#
# Invariantes que no se tocan, porque los leen el updater de cada app, el manifiesto de LGA_Updates
# y el card de LGA Updates de PipeSync:
#   - los nombres <ARTIFACT_NAME>_Mac_v<version>.zip/.dmg y SHA256SUMS, y el tag publico v<version>;
#   - el que publica segundo (Windows o Mac) SUMA al release existente: no lo recrea ni borra las
#     lineas de la otra plataforma en SHA256SUMS;
#   - el What's new se publica antes del refresco del manifiesto, que es el que lo anuncia.

show_help() {
    cat <<'EOF'
Uso: tools/macos/publish_release.sh (--preflight | --expect-head <sha>) [opciones]

  --preflight         Solo comprueba, antes de compilar. No escribe nada.
  --expect-head SHA   Publica. SHA es el HEAD del preflight: si el repo cambio, no se publica.
  --edition NOMBRE    Edicion (studio/client...) cuando release.conf define release_edition().
  --replace           Reemplazar los archivos de macOS si el release ya los tiene (re-publicar).
  --dry-run           Todo menos escribir: dice que haria en GitHub y en el repo.
  --yes               No preguntar en el control de las notas (What's new).
  --conf ARCHIVO      Otro release.conf (por defecto tools/macos/release.conf).
EOF
}

MODE=""
EXPECT_HEAD=""
EDITION=""
REPLACE=false
DRY_RUN=false
ASSUME_YES=false
CONF_FILE="tools/macos/release.conf"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --preflight) MODE="preflight"; shift ;;
        --expect-head)
            [[ $# -ge 2 ]] || { echo "ERROR: --expect-head requiere el SHA del preflight."; exit 1; }
            MODE="publish"; EXPECT_HEAD="$2"; shift 2 ;;
        --edition)
            [[ $# -ge 2 ]] || { echo "ERROR: --edition requiere un nombre."; exit 1; }
            EDITION="$2"; shift 2 ;;
        --replace) REPLACE=true; shift ;;
        --dry-run) DRY_RUN=true; shift ;;
        --yes|-y) ASSUME_YES=true; shift ;;
        --conf)
            [[ $# -ge 2 ]] || { echo "ERROR: --conf requiere un archivo."; exit 1; }
            CONF_FILE="$2"; shift 2 ;;
        -h|--help) show_help; exit 0 ;;
        *) echo "ERROR: opcion desconocida: $1"; show_help; exit 1 ;;
    esac
done

if [[ -z "$MODE" ]]; then
    echo "ERROR: falta --preflight o --expect-head."
    show_help
    exit 1
fi

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || { echo "ERROR: no estoy adentro de un repo git."; exit 1; }
cd "$REPO_ROOT"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---- Configuracion de la app -------------------------------------------------------------------
# Defaults: lo que vale para la mayoria. release.conf pisa lo que necesite.
APP_NAME=""
ARTIFACT_NAME=""
DEPLOY_DIR="deploy"
VERSION_FILE="VERSION"
VERSION_CMD=""            # si no esta vacio, la version es su salida (y VERSION_FILE no se usa)
VERSION_REGEX='^[0-9]+(\.[0-9]+)+$'
VERSION_CHECK_CMD=""
CODE_BRANCH="main"
RELEASE_REPO=""
SOURCE_TAG_PREFIX="v"
REQUIRE_PUBLIC_REPO=true
SHA256SUMS=false
WIN_ASSET=""
WHATS_NEW_FILE=""
REFRESH_MANIFEST=true
PINNED_CERT_SHA1=""       # si no esta vacio, ese certificado tiene que estar valido en el llavero
RELEASE_TITLE=""
RELEASE_NOTES=""
EXPECTED_GH_USER="legandrop"
EXPECTED_GIT_EMAIL="176236735+legandrop@users.noreply.github.com"

if [[ ! -f "$CONF_FILE" ]]; then
    echo "ERROR: falta $CONF_FILE (la configuracion de publicacion de esta app)."
    exit 1
fi
# shellcheck disable=SC1090
source "$CONF_FILE"
if [[ -n "$EDITION" ]]; then
    if ! declare -F release_edition >/dev/null; then
        echo "ERROR: --edition $EDITION, pero $CONF_FILE no define release_edition()."
        exit 1
    fi
    release_edition "$EDITION"
fi

for var in APP_NAME ARTIFACT_NAME RELEASE_REPO VERSION_CHECK_CMD; do
    if [[ -z "${!var}" ]]; then
        echo "ERROR: $CONF_FILE no define $var."
        exit 1
    fi
done

GH="${LGA_GH:-gh}"
if [[ -n "$VERSION_CMD" ]]; then
    APP_VERSION="$(bash -c "$VERSION_CMD" | tr -d '\r\n')"
elif [[ -f "$VERSION_FILE" ]]; then
    APP_VERSION="$(tr -d '\r\n' < "$VERSION_FILE")"
else
    echo "ERROR: falta $VERSION_FILE."
    exit 1
fi
if ! [[ "$APP_VERSION" =~ $VERSION_REGEX ]]; then
    echo "ERROR: $VERSION_FILE tiene un formato invalido: $APP_VERSION"
    exit 1
fi
TAG="v${APP_VERSION}"                       # tag PUBLICO, el que lee el manifiesto
SOURCE_TAG="${SOURCE_TAG_PREFIX}${APP_VERSION}"  # tag del repo de CODIGO
ZIP_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.zip"
DMG_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.dmg"
ZIP_PATH="${DEPLOY_DIR}/${ZIP_NAME}"
DMG_PATH="${DEPLOY_DIR}/${DMG_NAME}"
[[ -n "$RELEASE_TITLE" ]] || RELEASE_TITLE="$TAG"
[[ -n "$RELEASE_NOTES" ]] || RELEASE_NOTES="Release $TAG"
# {v} = la version, en los valores que la llevan (release.conf se lee antes de conocerla).
RELEASE_TITLE="${RELEASE_TITLE//\{v\}/$APP_VERSION}"
RELEASE_NOTES="${RELEASE_NOTES//\{v\}/$APP_VERSION}"
WIN_ASSET="${WIN_ASSET//\{v\}/$APP_VERSION}"

# El release vive en el MISMO repo del codigo (un solo tag) o en uno publico aparte (*_Release, con
# un tag privado en el repo de codigo y el publico en el de releases).
ORIGIN_URL="$(git config --get remote.origin.url 2>/dev/null || true)"
ORIGIN_SLUG="$(printf '%s\n' "$ORIGIN_URL" | sed -E 's#^(git@github\.com:|https://github\.com/)##; s#\.git$##')"
SAME_REPO=false
if [[ "$(printf '%s' "$ORIGIN_SLUG" | tr '[:upper:]' '[:lower:]')" == "$(printf '%s' "$RELEASE_REPO" | tr '[:upper:]' '[:lower:]')" ]]; then
    SAME_REPO=true
    if [[ "$SOURCE_TAG" != "$TAG" ]]; then
        echo "ERROR: con el release en el mismo repo hay un solo tag: SOURCE_TAG_PREFIX tiene que ser 'v'."
        exit 1
    fi
fi

WHATS_NEW_SH="${LGA_REPOTOOLS:-$REPO_ROOT/../LGA_RepoTools}/WhatsNew_Mac/whats_new_release.sh"

# ---- Escrituras: con --dry-run no se ejecuta ninguna ---------------------------------------------
gh_write() {
    if [[ "$DRY_RUN" == "true" ]]; then
        echo "  [dry-run] NO se ejecuta: gh $*"
        return 0
    fi
    "$GH" "$@"
}
git_write() {
    if [[ "$DRY_RUN" == "true" ]]; then
        echo "  [dry-run] NO se ejecuta: git $*"
        return 0
    fi
    git "$@"
}

# ---- SHA256SUMS ----------------------------------------------------------------------------------
# Las lineas que NO son de esta plataforma, mas las propias al final. Una sola linea por nombre de
# archivo; formato sha256sum ("hash  nombre"), LF. Una linea ilegible corta: no se adivina que era.
# Sin intervalos {64} en las regex: el awk de macOS no siempre los entiende.
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

# ---- Tag del repo de codigo ----------------------------------------------------------------------
# Deja TAG_REMOTE (true si SOURCE_TAG ya esta en origin) y TAG_SHA. Solo LEE.
#  - No existe: se crea sobre HEAD, que tiene que ser la punta de origin/CODE_BRANCH.
#  - Existe (lo creo Windows al publicar primero, o una corrida anterior): el release sale de ESE
#    codigo. HEAD tiene que ser ese commit, o -con el release en el mismo repo- uno posterior que
#    difiera SOLO en WHATS_NEW.md: las notas del primero que publico dejan un commit en main hecho
#    por la API, y quien llega segundo y hace `git pull` queda un commit adelante con el mismo codigo.
tag_check() {
    local refs head_sha branch_refs branch_sha diff extra
    TAG_REMOTE=false
    TAG_SHA=""
    head_sha="$(git rev-parse HEAD)"
    if ! refs="$(git ls-remote origin "refs/tags/$SOURCE_TAG" "refs/tags/$SOURCE_TAG^{}" 2>&1)"; then
        echo "ERROR: no se pudo leer los tags de origin: $refs"
        return 1
    fi
    TAG_SHA="$(printf '%s\n' "$refs" | awk -v t="refs/tags/$SOURCE_TAG" '$2 == t "^{}" { p = $1 } $2 == t { d = $1 } END { print (p != "" ? p : d) }')"
    if [[ -z "$TAG_SHA" ]]; then
        if git rev-parse -q --verify "refs/tags/$SOURCE_TAG" >/dev/null 2>&1; then
            echo "ERROR: existe un tag local $SOURCE_TAG que NO esta en origin (una corrida anterior que no llego a pushear)."
            echo "       Borrarlo (git tag -d $SOURCE_TAG) y volver a correr: se crea de nuevo sobre HEAD."
            return 1
        fi
        if ! branch_refs="$(git ls-remote origin "refs/heads/$CODE_BRANCH" 2>&1)"; then
            echo "ERROR: no se pudo leer $CODE_BRANCH de origin: $branch_refs"
            return 1
        fi
        branch_sha="$(printf '%s\n' "$branch_refs" | awk -v b="refs/heads/$CODE_BRANCH" '$2 == b { print $1 }')"
        if [[ "$branch_sha" != "$head_sha" ]]; then
            echo "ERROR: el tag $SOURCE_TAG no existe y HEAD no es la punta de origin/$CODE_BRANCH (el tag se crea sobre HEAD)."
            echo "       HEAD:              $head_sha"
            echo "       origin/$CODE_BRANCH: ${branch_sha:-?}"
            echo "       Hacer git pull o git push para que coincidan."
            return 1
        fi
        echo "Tag $SOURCE_TAG: no existe en origin; se crea (anotado) sobre HEAD ${head_sha:0:9}."
        return 0
    fi
    TAG_REMOTE=true
    if [[ "$TAG_SHA" == "$head_sha" ]]; then
        echo "Tag $SOURCE_TAG en origin: coincide con HEAD (${head_sha:0:9})."
        return 0
    fi
    if ! git cat-file -e "${TAG_SHA}^{commit}" 2>/dev/null; then
        echo "ERROR: el tag $SOURCE_TAG de origin apunta a $TAG_SHA, que no esta en este clon. Hacer git fetch."
        return 1
    fi
    if [[ "$SAME_REPO" == "true" ]] && git merge-base --is-ancestor "$TAG_SHA" HEAD \
        && diff="$(git diff --name-only "$TAG_SHA" HEAD)"; then
        extra="$(printf '%s\n' "$diff" | grep -vx -e 'WHATS_NEW.md' -e '' || true)"
        if [[ -z "$extra" ]]; then
            echo "Tag $SOURCE_TAG: ${TAG_SHA:0:9}. HEAD solo le agrega las notas (WHATS_NEW.md): mismo codigo."
            return 0
        fi
    fi
    echo "ERROR: el tag $SOURCE_TAG ya existe y apunta a otro codigo que HEAD."
    echo "       HEAD:               $head_sha"
    echo "       tag $SOURCE_TAG: $TAG_SHA"
    echo "       El release v$APP_VERSION ya salio (o lo empezo Windows) desde ese commit: publicar desde aca"
    echo "       subiria otro codigo con el mismo numero. Para re-publicar ESE codigo: git checkout $SOURCE_TAG."
    echo "       Para publicar el codigo nuevo: subir la version (bump) primero."
    return 1
}

# ---- Release en GitHub ---------------------------------------------------------------------------
# Deja REL_EXISTS, REL_SUMS y REL_HAS_WIN. Corta si es un borrador, si ya tiene lo de macOS (salvo
# --replace) o si tiene el instalador de Windows sin SHA256SUMS (fusionar contra nada borraria su
# linea). Que no exista es valido: se crea. Que exista sin Windows tambien: Windows suma despues.
release_check() {
    local out names draft
    REL_EXISTS=false
    REL_SUMS=false
    REL_HAS_WIN=false
    if ! out="$("$GH" release view "$TAG" --repo "$RELEASE_REPO" --json isDraft,assets 2>&1)"; then
        if printf '%s\n' "$out" | grep -qi "not found"; then
            echo "Release $TAG de $RELEASE_REPO: no existe; se crea."
            return 0
        fi
        echo "ERROR: no se pudo leer el release $TAG de $RELEASE_REPO: $out"
        return 1
    fi
    REL_EXISTS=true
    draft="$(printf '%s' "$out" | /usr/bin/python3 -c 'import json,sys; print(str(json.load(sys.stdin)["isDraft"]).lower())')"
    names="$(printf '%s' "$out" | /usr/bin/python3 -c 'import json,sys; [print(a["name"]) for a in json.load(sys.stdin)["assets"]]')"
    if [[ "$draft" == "true" ]]; then
        echo "ERROR: el release $TAG existe como borrador. Publicarlo o borrarlo a mano antes de seguir."
        return 1
    fi
    printf '%s\n' "$names" | grep -qx 'SHA256SUMS' && REL_SUMS=true
    [[ -n "$WIN_ASSET" ]] && printf '%s\n' "$names" | grep -qx "$WIN_ASSET" && REL_HAS_WIN=true
    if printf '%s\n' "$names" | grep -qx -e "$ZIP_NAME" -e "$DMG_NAME"; then
        if [[ "$REPLACE" != "true" ]]; then
            echo "ERROR: el release $TAG ya tiene los archivos de macOS. Para reemplazarlos (re-publicar), --replace."
            return 1
        fi
        echo "Release $TAG: ya tiene los archivos de macOS; se reemplazan (--replace)."
    fi
    if [[ "$SHA256SUMS" == "true" && "$REL_HAS_WIN" == "true" && "$REL_SUMS" != "true" ]]; then
        echo "ERROR: el release $TAG tiene $WIN_ASSET pero no SHA256SUMS: fusionar contra nada borraria su"
        echo "       linea. Subir primero el SHA256SUMS de Windows."
        return 1
    fi
    if [[ "$REL_HAS_WIN" == "true" ]]; then
        echo "Release $TAG de $RELEASE_REPO: existe, con $WIN_ASSET; se suma macOS."
    else
        echo "Release $TAG de $RELEASE_REPO: existe; se suma macOS."
    fi
    return 0
}

# Con el release en el mismo repo, un release publicado siempre tiene su tag: si el release existe y
# el tag no, alguien lo borro y no se sabe de que codigo salio. Con el release aparte, el tag publico
# sin release es el otro caso raro (una corrida que creo el tag y no el release).
state_consistent() {
    if [[ "$SAME_REPO" == "true" ]]; then
        if [[ "$REL_EXISTS" == "true" && "$TAG_REMOTE" != "true" ]]; then
            echo "ERROR: el release $TAG existe pero su tag no esta en origin. No se toca: revisarlo a mano."
            return 1
        fi
        return 0
    fi
    if [[ "$REL_EXISTS" != "true" ]] && "$GH" api "repos/$RELEASE_REPO/git/refs/tags/$TAG" >/dev/null 2>&1; then
        echo "ERROR: $RELEASE_REPO tiene el tag $TAG pero no el release (una corrida anterior quedo a medias)."
        echo "       Revisarlo a mano: borrar el tag publico o crear el release a mano."
        return 1
    fi
    return 0
}

PRE_ERRORS=0
pre_fail() {
    echo "ERROR: $1"
    PRE_ERRORS=$((PRE_ERRORS + 1))
}

# ---- Preflight -----------------------------------------------------------------------------------
# Junta TODOS los errores en vez de cortar en el primero. Solo LEE.
preflight() {
    local status branch head_sha gh_user email vis default_branch out
    echo "Comprobaciones previas de la publicacion de $APP_NAME $TAG (macOS) en $RELEASE_REPO..."

    local gh_ok=false
    if ! command -v "$GH" >/dev/null 2>&1 || ! "$GH" auth status >/dev/null 2>&1; then
        pre_fail "publicar necesita gh (GitHub CLI) instalado y con login (gh auth login)."
    else
        gh_ok=true
        gh_user="$("$GH" api user -q .login 2>/dev/null || true)"
        if [[ -n "$EXPECTED_GH_USER" && "$gh_user" != "$EXPECTED_GH_USER" ]]; then
            pre_fail "gh esta logueado como '${gh_user:-?}', no como $EXPECTED_GH_USER."
        fi
    fi
    email="$(git config user.email || true)"
    if [[ -n "$EXPECTED_GIT_EMAIL" && "$email" != "$EXPECTED_GIT_EMAIL" ]]; then
        pre_fail "git user.email es '${email:-?}', no $EXPECTED_GIT_EMAIL."
    fi

    status="$(git status --porcelain)"
    if [[ -n "$status" ]]; then
        pre_fail "hay cambios sin commitear; el release tiene que salir de un commit:"
        printf '%s\n' "$status" | head -20 | sed 's/^/       /'
    fi
    git fetch -q origin "refs/tags/$SOURCE_TAG:refs/tags/$SOURCE_TAG" 2>/dev/null || true

    # La rama importa solo si el tag se crea ahora: re-publicar el codigo de un tag existente se
    # hace con ese commit afuera (git checkout <tag>), fuera de la rama.
    TAG_REMOTE=false
    local tag_ok=true
    if ! tag_check; then
        tag_ok=false
        PRE_ERRORS=$((PRE_ERRORS + 1))
    fi
    if [[ "$tag_ok" == "true" && "$TAG_REMOTE" != "true" ]]; then
        branch="$(git symbolic-ref --short -q HEAD || true)"
        if [[ "$branch" != "$CODE_BRANCH" ]]; then
            pre_fail "la rama actual es '${branch:-HEAD suelto}', no $CODE_BRANCH: un tag nuevo se crea desde $CODE_BRANCH."
        fi
    fi

    if ! out="$(bash -c "$VERSION_CHECK_CMD" 2>&1)"; then
        pre_fail "la version no esta lista para publicar (falta el bump o no esta commiteado):"
        printf '%s\n' "$out" | sed 's/^/       /'
    fi

    if [[ -n "$PINNED_CERT_SHA1" ]] && ! security find-identity -v -p codesigning 2>/dev/null | grep -qi "$PINNED_CERT_SHA1"; then
        pre_fail "el certificado fijado ($PINNED_CERT_SHA1) no esta, valido, en el llavero: no se publica con otro."
    fi

    if [[ -n "$WHATS_NEW_FILE" ]]; then
        [[ -f "$WHATS_NEW_FILE" ]] || pre_fail "falta $WHATS_NEW_FILE (las notas para el usuario de v$APP_VERSION)."
        [[ -f "$WHATS_NEW_SH" ]] || pre_fail "no encontre $WHATS_NEW_SH (clonar LGA_RepoTools al lado de este repo o definir LGA_REPOTOOLS)."
    fi

    if [[ "$gh_ok" == "true" ]]; then
        if [[ "$REQUIRE_PUBLIC_REPO" == "true" ]]; then
            vis="$("$GH" repo view "$RELEASE_REPO" --json visibility -q .visibility 2>/dev/null || true)"
            if [[ "$vis" != "PUBLIC" ]]; then
                pre_fail "$RELEASE_REPO no es publico (${vis:-no se pudo leer}): el updater y el manifiesto no verian el release."
            fi
            if [[ "$SAME_REPO" != "true" ]]; then
                default_branch="$("$GH" repo view "$RELEASE_REPO" --json defaultBranchRef -q .defaultBranchRef.name 2>/dev/null || true)"
                if [[ -z "$default_branch" ]]; then
                    pre_fail "$RELEASE_REPO esta vacio (sin rama por defecto): no se le puede crear un release."
                fi
            fi
        fi
        local rel_ok=true
        if ! release_check; then
            rel_ok=false
            PRE_ERRORS=$((PRE_ERRORS + 1))
        fi
        if [[ "$tag_ok" == "true" && "$rel_ok" == "true" ]] && ! state_consistent; then
            PRE_ERRORS=$((PRE_ERRORS + 1))
        fi
    fi

    if [[ "$PRE_ERRORS" -gt 0 ]]; then
        echo ""
        echo "No se publica: $PRE_ERRORS problema(s) arriba. No se compilo ni se subio nada."
        exit 1
    fi

    # Recien con todo en orden, el control de las notas (puede preguntar).
    if [[ -n "$WHATS_NEW_FILE" ]]; then
        echo "Verificando las notas para el usuario (What's new) de v$APP_VERSION..."
        local wn_args=(check "$WHATS_NEW_FILE" "$APP_VERSION")
        [[ "$ASSUME_YES" == "true" ]] && wn_args+=(--yes)
        if ! sh "$WHATS_NEW_SH" "${wn_args[@]}"; then
            echo "ERROR: faltan o fallan las notas de v$APP_VERSION, o se contesto que no. No se compilo ni se subio nada."
            exit 1
        fi
    fi
    echo "Preflight OK: se puede publicar $TAG."
}

# ---- Publicar ------------------------------------------------------------------------------------
publish() {
    local work="" old sums lines names a clobber=""
    local assets=("$ZIP_PATH" "$DMG_PATH")
    for a in "${assets[@]}"; do
        [[ -f "$a" ]] || { echo "ERROR: falta $a"; return 1; }
    done
    [[ "$REPLACE" == "true" ]] && clobber="--clobber"

    # Nada cambio desde el preflight (la compilacion tarda minutos).
    if [[ "$(git rev-parse HEAD)" != "$EXPECT_HEAD" ]] || [[ -n "$(git status --porcelain)" ]]; then
        echo "ERROR: el repo cambio durante el deploy (HEAD o archivos). No se publica."
        return 1
    fi
    # Lo mismo en GitHub: Windows pudo crear el tag o el release en el medio.
    tag_check || return 1
    release_check || return 1
    state_consistent || return 1

    # Lo que baja el auto-update: el archivo EXACTO que se sube.
    bash "$SCRIPT_DIR/verify_bundle_signature.sh" "$ZIP_PATH" || return 1

    sums=""
    if [[ "$SHA256SUMS" == "true" ]]; then
        (cd "$DEPLOY_DIR" && shasum -a 256 "$ZIP_NAME" "$DMG_NAME") > "$DEPLOY_DIR/SHA256SUMS.mac"
        sums="$DEPLOY_DIR/SHA256SUMS"
        work="$(mktemp -d)"
        old=/dev/null
        if [[ "$REL_SUMS" == "true" ]]; then
            "$GH" release download "$TAG" --repo "$RELEASE_REPO" --pattern SHA256SUMS --dir "$work" \
                || { rm -rf "$work"; echo "ERROR: no se pudo bajar el SHA256SUMS del release."; return 1; }
            old="$work/SHA256SUMS"
        fi
        merge_sha256sums "$old" "$DEPLOY_DIR/SHA256SUMS.mac" "$sums" || { rm -rf "$work"; return 1; }
        rm -rf "$work"
        if [[ "$REL_HAS_WIN" == "true" ]] && ! grep -q "  ${WIN_ASSET}\$" "$sums"; then
            echo "ERROR: el SHA256SUMS fusionado perdio la linea de $WIN_ASSET (se romperia el update de Windows)."
            return 1
        fi
        lines="$(wc -l < "$sums" | tr -d ' ')"
    fi

    echo ""
    if [[ "$DRY_RUN" == "true" ]]; then
        echo "=== --dry-run: esto es lo que SE HARIA en $RELEASE_REPO para $TAG ==="
    elif [[ "$REL_EXISTS" == "true" ]]; then
        echo "Subiendo a $RELEASE_REPO $TAG..."
    else
        echo "Creando el release $TAG en $RELEASE_REPO..."
    fi
    for a in "${assets[@]}"; do
        echo "  $(basename "$a")  ($(du -h "$a" | cut -f1))"
    done
    if [[ -n "$sums" ]]; then
        echo "  SHA256SUMS ($lines lineas):"
        sed 's/^/    /' "$sums"
    fi

    # El tag del repo de codigo, si no existe. Si el push falla se borra el local: no queda nada a medias.
    if [[ "$TAG_REMOTE" != "true" ]]; then
        git_write tag -a "$SOURCE_TAG" -m "Release $TAG" "$EXPECT_HEAD" \
            || { echo "ERROR: no se pudo crear el tag $SOURCE_TAG."; return 1; }
        if ! git_write push origin "$SOURCE_TAG"; then
            git tag -d "$SOURCE_TAG" >/dev/null 2>&1 || true
            echo "ERROR: no se pudo pushear el tag $SOURCE_TAG. Se borro el local; no se creo nada en GitHub."
            return 1
        fi
    fi

    local upload=("${assets[@]}")
    [[ -n "$sums" ]] && upload+=("$sums")
    if [[ "$REL_EXISTS" != "true" ]]; then
        # Mismo repo: el release usa el tag recien pusheado. Repo aparte: GitHub crea el tag publico
        # sobre la rama por defecto del repo de releases.
        local target_args=(--verify-tag)
        [[ "$SAME_REPO" == "true" ]] || target_args=(--target "$("$GH" repo view "$RELEASE_REPO" --json defaultBranchRef -q .defaultBranchRef.name)")
        if ! gh_write release create "$TAG" "${upload[@]}" --repo "$RELEASE_REPO" "${target_args[@]}" \
                --title "$RELEASE_TITLE" --notes "$RELEASE_NOTES"; then
            echo "ERROR: no se pudo crear el release $TAG. El tag $SOURCE_TAG ya quedo en origin: reintentar con el"
            echo "       mismo comando, que lo encuentra y solo crea el release. Si GitHub dejo un borrador, borrarlo."
            return 1
        fi
    else
        gh_write release upload "$TAG" "${assets[@]}" --repo "$RELEASE_REPO" ${clobber:+$clobber} \
            || { echo "ERROR: no se pudieron subir el .zip y el .dmg al release $TAG."; return 1; }
        if [[ -n "$sums" ]] && ! gh_write release upload "$TAG" "$sums" --repo "$RELEASE_REPO" --clobber; then
            echo "ERROR: el .zip y el .dmg subieron, pero SHA256SUMS no. El fusionado esta en $REPO_ROOT/$sums:"
            echo "       gh release upload $TAG \"$REPO_ROOT/$sums\" --repo $RELEASE_REPO --clobber"
            return 1
        fi
    fi

    local notes_failed=false
    if [[ -n "$WHATS_NEW_FILE" ]]; then
        echo "Publicando las notas para el usuario (What's new)..."
        local wn_args=(publish "$WHATS_NEW_FILE" "$APP_VERSION" "$RELEASE_REPO" "$TAG")
        [[ "$DRY_RUN" == "true" ]] && wn_args+=(--dry-run)
        if ! sh "$WHATS_NEW_SH" "${wn_args[@]}"; then
            echo "ERROR: el release $TAG quedo con el .zip y el .dmg, pero sus notas no. Reintentar con el comando de arriba."
            notes_failed=true
        fi
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        [[ "$REFRESH_MANIFEST" == "true" ]] && echo "  [dry-run] NO se ejecuta: gh workflow run refresh_versions.yml --repo legandrop/LGA_Updates"
        echo ""
        echo "--dry-run: todas las comprobaciones pasaron. No se creo, subio ni escribio nada."
        return 0
    fi

    names="$("$GH" release view "$TAG" --repo "$RELEASE_REPO" --json assets -q '.assets[].name' 2>/dev/null | tr -d '\r')" || names=""
    for a in "$ZIP_NAME" "$DMG_NAME" ${sums:+SHA256SUMS}; do
        printf '%s\n' "$names" | grep -qx "$a" || echo "AVISO: el release $TAG no lista $a despues de subir. Revisarlo."
    done
    if [[ -n "$WIN_ASSET" ]] && ! printf '%s\n' "$names" | grep -qx "$WIN_ASSET"; then
        echo "Falta el instalador de Windows ($WIN_ASSET): lo suma Windows sobre este mismo release."
    fi

    if [[ "$REFRESH_MANIFEST" == "true" ]]; then
        # Sin esto hay que esperar al cron de LGA_Updates. Si falla, el cron lo levanta igual.
        if "$GH" workflow run refresh_versions.yml --repo legandrop/LGA_Updates >/dev/null 2>&1 </dev/null; then
            echo "Manifiesto de versiones: refresco disparado."
        else
            echo "AVISO: no se pudo disparar el refresco del manifiesto. El cron lo levanta solo."
        fi
    fi
    if [[ "$SAME_REPO" == "true" && -n "$WHATS_NEW_FILE" ]]; then
        echo "Las notas dejaron un commit en origin/$CODE_BRANCH (WHATS_NEW.md): correr git pull."
    fi
    echo "Release publicado: https://github.com/${RELEASE_REPO}/releases/tag/${TAG}"
    [[ "$notes_failed" == "true" ]] && return 1
    return 0
}

if [[ "$MODE" == "preflight" ]]; then
    preflight
else
    publish
fi

<#
.SYNOPSIS
    Bump de version de VideoDownloader para publicar un release. Lo corre Lega cuando decide
    publicar: nunca se corre solo ni en cada cambio.
.DESCRIPTION
    Los dos numeros significan cosas distintas:
      - CMakeLists.txt (y VERSION, que lo copia): la ultima version PUBLICADA.
      - El primer "vX.YYY:" de docs/ChangeLog.md: la version EN DESARROLLO, donde se juntan las
        entradas hasta el proximo release. Que el changelog vaya adelante es el estado normal.
    El bump sube CMakeLists.txt y VERSION a la version del changelog. Es UNO solo para Windows y
    macOS: las dos plataformas publican en el mismo release v<version>, en cualquier orden.

    Misma logica que bump_version.sh (el de macOS), sin Python. La version va de a milesimos (1.000,
    1.001...): con otro formato el script falla. La unica excepcion es la version publicada de dos
    decimales anterior al cambio de numeracion (0.95), que vale como 0.950. Pasar a la mayor
    siguiente (X.000) no se toma como salto. Sin consola interactiva, toda pregunta se contesta que no.
.PARAMETER Check
    No escribe nada: sale 0 si CMakeLists.txt, VERSION y el changelog dicen lo mismo y el bump esta
    commiteado (listo para publicar), 1 si no. Lo usan instalador.bat y deploy.sh antes de publicar.
.PARAMETER Commit
    Commitea y pushea el bump sin preguntar.
.PARAMETER NoCommit
    Deja el bump sin commitear y no pregunta (tampoco ofrece publicar).
.PARAMETER Publish
    Despues del push corre instalador.bat --publish sin preguntar (instalador y release de Windows).
.PARAMETER NoPublish
    No publica ni pregunta.
#>
param(
    [switch]$Check,
    [switch]$Commit,
    [switch]$NoCommit,
    [switch]$Publish,
    [switch]$NoPublish
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$changelogPath = Join-Path $root 'docs\ChangeLog.md'
$cmakePath = Join-Path $root 'CMakeLists.txt'
$versionPath = Join-Path $root 'VERSION'
$expectedEmail = '176236735+legandrop@users.noreply.github.com'
$utf8 = New-Object System.Text.UTF8Encoding($false)

$headerPattern = '(?m)^v([0-9]+\.[0-9]+)\s*:'
$cmakePattern = 'project\(\s*VideoDownloader\s+VERSION\s+([0-9]+\.[0-9]+)'

$commitMode = if ($NoCommit) { 'no' } elseif ($Commit) { 'yes' } else { 'ask' }
$publishMode = if ($NoPublish) { 'no' } elseif ($Publish) { 'yes' } else { 'ask' }

# El minor se completa A LA DERECHA hasta tres digitos, como el auto-update: 0.95 vale 0.950.
function Get-Key([string]$version) {
    $parts = $version.Split('.')
    return [int]$parts[0] * 1000 + [int]$parts[1].PadRight(3, '0')
}

# Pregunta Y/N con una sola tecla, como `choice /C YN`. Sin consola interactiva contesta que no.
function Ask([string]$question) {
    if ([Console]::IsInputRedirected) {
        Write-Host "$question [Y,N]? N (sin consola)"
        return $false
    }
    while ($true) {
        Write-Host "$question [Y,N]? " -NoNewline
        $key = [Console]::ReadKey($true).KeyChar.ToString().ToUpper()
        if ($key -eq 'Y' -or $key -eq 'S') { Write-Host 'Y'; return $true }
        if ($key -eq 'N') { Write-Host 'N'; return $false }
        Write-Host ''
    }
}

# En PowerShell 5.1, con 'Stop', lo que git escribe por stderr se vuelve una excepcion: aca se decide
# por el codigo de salida.
function Invoke-Git {
    $ErrorActionPreference = 'Continue'
    $out = & git -C $root @args 2>&1 | ForEach-Object { "$_" }
    $script:gitExit = $LASTEXITCODE
    return $out
}

try {
    $changelog = [IO.File]::ReadAllText($changelogPath, $utf8)
    $cmake = [IO.File]::ReadAllText($cmakePath, $utf8)

    $m = [regex]::Match($changelog, $headerPattern)
    if (-not $m.Success) { throw 'No se encontro un encabezado "vX.YYY:" en docs/ChangeLog.md' }
    $changelogVersion = $m.Groups[1].Value
    $m = [regex]::Match($cmake, $cmakePattern)
    if (-not $m.Success) { throw 'No se encontro project(VideoDownloader VERSION x.yyy) en CMakeLists.txt' }
    $cmakeVersion = $m.Groups[1].Value
    $fileVersion = if (Test-Path $versionPath) { ([IO.File]::ReadAllText($versionPath, $utf8)).Trim() } else { '' }

    if ($changelogVersion -notmatch '^[0-9]+\.[0-9]{3}$') {
        throw "La version del changelog '$changelogVersion' no tiene tres decimales (se numera 1.000, 1.001...)"
    }
    if ($cmakeVersion -notmatch '^[0-9]+\.[0-9]{2,3}$') {
        throw "La version de CMakeLists.txt '$cmakeVersion' no tiene tres decimales (se numera 1.000, 1.001...)"
    }
    $shownFile = if ($fileVersion) { $fileVersion } else { 'falta' }
    Write-Host "[bump_version] Publicada (CMakeLists.txt): $cmakeVersion | En desarrollo (changelog): $changelogVersion | VERSION: $shownFile"

    if ($Check) {
        if ($cmakeVersion -ne $changelogVersion -or $fileVersion -ne $changelogVersion) {
            Write-Host '[bump_version] ERROR: CMakeLists.txt, VERSION y el changelog no dicen lo mismo: falta correr bump_version.bat y commitear.'
            exit 1
        }
        # Un bump hecho pero sin commitear no sirve para publicar: el tag apuntaria a otro estado.
        $null = Invoke-Git diff --quiet HEAD -- CMakeLists.txt VERSION
        if ($gitExit -ne 0) {
            Write-Host "[bump_version] ERROR: el bump a $changelogVersion esta hecho pero sin commitear: commitearlo, o descartarlo con git checkout -- CMakeLists.txt VERSION y volver a correr bump_version.bat."
            exit 1
        }
        Write-Host "[bump_version] OK: todo en $changelogVersion."
        exit 0
    }

    # Antes de escribir nada, si se va a commitear: el bump sale de main al dia con origin (si la otra
    # maquina ya bumpeo y publico, aca falta un pull), del changelog commiteado (si no, se pushea un main
    # donde CMakeLists.txt y el changelog no coinciden), de CMakeLists.txt y VERSION sin otros cambios
    # (el commit los lleva enteros) y con la identidad de legandrop.
    if ($commitMode -ne 'no') {
        $branch = (Invoke-Git rev-parse --abbrev-ref HEAD | Out-String).Trim()
        if ($branch -ne 'main') { throw "La rama es '$branch': el bump se hace en main." }
        $out = Invoke-Git fetch -q origin main
        if ($gitExit -ne 0) { throw "No se pudo leer origin/main. $out" }
        $null = Invoke-Git merge-base --is-ancestor origin/main HEAD
        if ($gitExit -ne 0) { throw 'main esta atras de origin/main: hacer git pull antes del bump.' }
        $null = Invoke-Git diff --quiet HEAD -- docs/ChangeLog.md
        if ($gitExit -ne 0) { throw 'docs/ChangeLog.md tiene cambios sin commitear: commitearlos antes del bump.' }
        $null = Invoke-Git diff --quiet HEAD -- CMakeLists.txt VERSION
        if ($gitExit -ne 0) { throw 'CMakeLists.txt o VERSION tienen cambios sin commitear: commitearlos o descartarlos antes del bump (si son de un bump anterior sin commitear, alcanza con git checkout -- CMakeLists.txt VERSION y volver a correrlo).' }
        # GitHub atribuye por email: con otra identidad el commit queda fuera de la cuenta.
        $email = (Invoke-Git config user.email | Out-String).Trim()
        if ($email -ne $expectedEmail) { throw "git user.email es '$email' y se esperaba '$expectedEmail'." }
    }

    if ((Get-Key $changelogVersion) -lt (Get-Key $cmakeVersion)) {
        throw "El changelog (v$changelogVersion) esta por debajo de la version publicada ($cmakeVersion). Revisarlo a mano."
    }

    if ($changelogVersion -eq $cmakeVersion) {
        Write-Host "[bump_version] CMakeLists.txt ya esta en ${cmakeVersion}: no hay version nueva que subir."
        Write-Host '               Para un release nuevo, el changelog tiene que abrir v<siguiente>: con sus entradas.'
    }
    else {
        $n = (Get-Key $cmakeVersion) + 1
        $next = '{0}.{1:D3}' -f [int][math]::Floor($n / 1000), ($n % 1000)
        $clParts = $changelogVersion.Split('.')
        $majorStep = ([int]$clParts[1] -eq 0) -and ([int]$clParts[0] -eq [int]$cmakeVersion.Split('.')[0] + 1)
        if ($changelogVersion -ne $next -and -not $majorStep) {
            Write-Host "[bump_version] AVISO: el changelog salta de $cmakeVersion a $changelogVersion (lo esperable era $next)."
            if (-not (Ask "Subir igual a $changelogVersion")) { throw 'Bump cancelado.' }
        }
        $newCmake = ([regex]$cmakePattern).Replace($cmake, { param($x) $x.Value.Substring(0, $x.Value.Length - $x.Groups[1].Value.Length) + $changelogVersion }, 1)
        [IO.File]::WriteAllText($cmakePath, $newCmake, $utf8)
        Write-Host "[bump_version] Bump: $cmakeVersion -> $changelogVersion"
    }
    if ($fileVersion -ne $changelogVersion) { [IO.File]::WriteAllText($versionPath, "$changelogVersion`n", $utf8) }
    $version = $changelogVersion

    # ---- Commit y push: el commit lleva SOLO los archivos del bump, nunca un `git add -A` que arrastre
    # trabajo en curso. El push lleva ademas los commits locales previos, que se listan antes.
    if ($commitMode -eq 'no') { Write-Host '[bump_version] Sin commit: los cambios quedan en el arbol.'; exit 0 }
    $status = Invoke-Git status --porcelain -- CMakeLists.txt VERSION
    if ($gitExit -ne 0) { throw "git status fallo: $status" }
    $changed = @($status | Where-Object { $_ -match '^.. \S' } | ForEach-Object { $_.Substring(3).Trim().Trim('"') })
    if ($changed.Count -gt 0) {
        Write-Host '[bump_version] Archivos del bump:'
        $changed | ForEach-Object { Write-Host "  $_" }
        $ahead = @(Invoke-Git log '--format=  %h %s' origin/main..HEAD | Where-Object { $_ -match '\S' })
        if ($ahead.Count -gt 0) {
            Write-Host '[bump_version] El push lleva tambien estos commits locales que todavia no estan en origin:'
            $ahead | ForEach-Object { Write-Host $_ }
        }
        if ($commitMode -eq 'ask' -and -not (Ask "Commitear y pushear el bump a v$version")) {
            Write-Host '[bump_version] Commit omitido: los cambios quedan en el arbol.'
            exit 0
        }
        $out = Invoke-Git add -- @changed
        if ($gitExit -ne 0) { throw "git add fallo: $out" }
        $out = Invoke-Git commit -q -m "Version - Bump a v$version" -- @changed
        if ($gitExit -ne 0) { throw "Fallo el commit; los cambios quedan stageados. $out" }
        Write-Host (Invoke-Git log -1 '--format=[bump_version] Commit %h: %s')
        $out = Invoke-Git push -q origin main
        if ($gitExit -ne 0) { throw "Fallo el push; el commit queda local. Revisar credenciales y reintentar a mano. $out" }
        Write-Host '[bump_version] Pusheado.'
    }
    else {
        Write-Host '[bump_version] No hay cambios de version para commitear.'
    }

    # ---- Publicar: el release v<version> es uno solo para las dos plataformas. Desde Windows lo arma
    # instalador.bat (interactivo: hereda esta consola); los archivos de macOS los sube deploy.sh --publish.
    if ($publishMode -eq 'no') { exit 0 }
    if ($publishMode -eq 'ask' -and -not (Ask "Correr instalador.bat --publish (instalador y release v$version de Windows)")) {
        Write-Host '[bump_version] Publicacion omitida. Para hacerla despues: instalador.bat --publish'
        exit 0
    }
    & (Join-Path $root 'instalador.bat') --publish
    exit $LASTEXITCODE
}
catch {
    Write-Host "[bump_version] ERROR: $($_.Exception.Message)"
    exit 1
}

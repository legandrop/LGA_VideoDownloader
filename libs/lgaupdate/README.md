# lgaupdate — auto-update de las apps LGA

**Pieza canónica de `LGA_Base_QT_C_Py`, rev 1 (2026-10-15).** Cada app la copia **idéntica** a su
`libs/lgaupdate`, este README incluido, y no la edita: un cambio se hace acá, se sube la revisión
y se vuelve a copiar a todas. Así comparar una copia con la canónica es un `diff -r` sin
excepciones.

```bash
rsync -a --delete ../LGA_Base_QT_C_Py/libs/lgaupdate/ libs/lgaupdate/
diff -r ../LGA_Base_QT_C_Py/libs/lgaupdate libs/lgaupdate && echo identica
```

La usan FrameRev, Media Tools (con el Shot Player), Theta Explorer y Video Downloader. FileManager
S3 y PipeSync siguen con su `FM_UpdateService`, del que esta biblioteca es un port.

## Qué hace

Chequeo contra el manifiesto `legandrop.github.io/LGA_Updates/versions.json` (nunca contra la API
de GitHub), cartel "Update Available" con notas (Update now / Remind me later / Skip this version /
Cancel), descarga por streaming con SHA-256 del manifiesto (fail-closed: sin hash no se ofrece),
y un helper que espera a que la app se cierre:

- **Windows**: baja `<productId>_Setup_v<x>.exe` y lo abre con su wizard. Si sigue abierta otra
  ventana de la app, **posterga** el update con un aviso (en macOS, una notificación); nunca la
  cierra a la fuerza.
- **macOS**: baja `<productId>_Mac_v<x>.zip`, lo extrae con `ditto`, corre `codesign --verify
  --deep --strict`, reemplaza el bundle instalado (con rollback) y reabre la app. El bundle se
  busca por glob dentro del ZIP: el nombre del `.app` no tiene que coincidir con `productId`,
  pero el bundle instalado no se renombra (el helper reemplaza el que está corriendo).

Además: chequeo periódico por reloj de pared (3 h ± 15 min), "What's new" una vez después de
instalar, historial completo a pedido, y barrido de la carpeta de descargas
(`sweepUpdateCache`).

## Contrato con la app (`LgaUpdateConfig`)

| Campo | Para qué |
|---|---|
| `productId`, `displayName`, `releaseRepoSlug`, `windowsExecutableName` | Identidad del asset, nombre visible, repo de releases (clave en el manifiesto) y exe que el helper espera en Windows |
| `settingsPath`, `updateCacheDir` | `.ini` de la app (`Updates/*`) y carpeta de descargas (en Windows, dentro de la instalación; en macOS, fuera del bundle) |
| `presentDialog` | **Los carteles**. La biblioteca arma un `LgaUpdateDialogSpec` (tipo, título, mensaje HTML, notas, detalle, botones con rol) y la app lo dibuja con sus propios diálogos. Sin él cae a un `QMessageBox` |
| `emphasizeHtml` | Cómo se resalta un dato en el mensaje (default `<b>`) |
| `updateBlockedReason` | Por qué este binario no se actualiza (árbol de desarrollo) |
| `installBlockedReason` | Por qué no se puede instalar ahora (trabajo sin guardar, descargas en curso). Se reevalúa al elegir Update now y al terminar la descarga |
| `prepareForUpdatePrompt`, `quitApplication` | Preparar la UI antes del cartel; camino de cierre propio |
| `relaunchWhenUpdatePostponed` | Si el helper posterga porque otra instancia sigue abierta, reabrir la que se cerró (una residente como FrameRev). Default false |
| `automaticChecksEnabled`, `networkFactory`, `busyCursorOnManualCheck` | Preferencia del usuario, red de prueba, cursor de espera para apps sin botón "Checking..." |

Señales: `checkStarted` / `checkFinished` (para un botón "Check now"), y `updateAvailable(version)`
para un aviso que no interrumpe (sale aunque el usuario haya pospuesto el cartel).

El presentador decide por `spec.kind` y por el **rol** de cada botón, no por su posición: cada app
ordena los botones a su manera y el servicio interpreta el rol del elegido.

## Historia

Antes de la rev 1 había tres copias que ya diferían: la de Theta Explorer (la más nueva), la de
FrameRev (igual en lógica, con el cartel de `Dialogs` adentro) y la de Media Tools (la más vieja:
carteles propios dentro de la biblioteca, sin `installBlockedReason` y con cierre forzado de la app
en el helper). La rev 1 parte de la de Theta Explorer (`e1547cb`), saca el cartel de la app a
`presentDialog`, y suma `emphasizeHtml` (FrameRev), `busyCursorOnManualCheck` (Media Tools) y
`updateAvailable` (Video Downloader). Cada app conserva sus carteles: Theta Explorer y Video
Downloader traducen la spec a su `ConfirmDialog`, FrameRev a `Dialogs` (`UpdateDialogPresenter`) y
Media Tools a `libs/lgaupdateui`, que antes vivía adentro de la biblioteca.

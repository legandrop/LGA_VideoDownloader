# lgaupdate

Biblioteca de auto-update compartida por las apps Qt de LGA: chequeo contra el manifiesto de
`legandrop/LGA_Updates`, cartel de version nueva con las notas (What's new), descarga con
SHA-256 y reemplazo de la instalacion con un helper (instalador Inno en Windows, ZIP firmado en
macOS). El uso en esta app esta en `docs/Doc_Auto_Update.md`.

## Origen

Copia de `libs/lgaupdate` de **`LGA_ThetaExplorer`**, ultimo commit que toco la carpeta
`36f61b6` (2026-10-13), tomada con el repo en `e1547cb` el 2026-10-15. Se eligio esa copia entre
las tres que existian (ThetaExplorer, FrameRev y MediaTools) porque es la mas nueva y la unica
con `networkFactory`, `installBlockedReason`, `prepareForUpdatePrompt`, `quitApplication`, el
rechazo de un asset sin SHA-256 y el diagnostico de un manifiesto ilegible.

## Diferencias con esa copia

Solo dos, para que alinear las copias en una pieza canonica de `LGA_Base_QT_C_Py` sea un diff
corto:

1. **El cartel**: `src/LgaUpdateService.cpp` incluye `videodownloader/ConfirmDialog.h` en vez de
   `thetaexplorer/ConfirmDialog.h`. La biblioteca usa el cartel de la app que la linkea (FrameRev
   usa su `Dialogs`, MediaTools trae `LgaUpdateDialogs` adentro). El de esta app tiene la misma
   API que el de ThetaExplorer (`include/videodownloader/ConfirmDialog.h`).
2. **La senal `updateAvailable(version)`** (aditiva): sale cuando un chequeo encuentra una version
   instalable mas nueva, antes del snooze y el skip. Esta app la usa para el aviso del header, que
   sigue visible aunque el usuario haya pospuesto el cartel. Las apps que no la conectan no cambian.

Cualquier otro cambio a la biblioteca tiene que servir para todas las apps: se hace en la pieza
canonica, no aca.

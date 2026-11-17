@echo off
rem Lo que el usuario veria en What's New si se publica ahora: los items de docs\WhatsNew.md desde la
rem ultima version publicada en GitHub, con colores en consola. Exit 0 = hay items, 3 = no hay nada
rem nuevo (publicar no tiene sentido), 1 = error. Version: el primer argumento o, sin argumento, la
rem seccion ## vX mas alta del WhatsNew.md (la version en desarrollo). No se usa VERSION: en las apps
rem de release manual dice la publicada hasta el bump, y esto se mira ANTES del bump.
rem Plantilla de LGA_Base_QT_C_Py: una app con el WhatsNew.md en otra ruta cambia solo --file.
rem Doc: docs\Doc_Instaladores_Inno.md, seccion 12. LGA_REPOTOOLS permite apuntar a otra copia.
setlocal
if defined LGA_REPOTOOLS (set "WN=%LGA_REPOTOOLS%") else set "WN=%~dp0..\LGA_RepoTools"
set "WN_VERSION="
if not "%~1"=="" set "WN_VERSION=--version %~1"
call "%WN%\WhatsNew_Win\build_whats_new.bat" pending --file "%~dp0docs\WhatsNew.md" %WN_VERSION%
exit /b %ERRORLEVEL%

---
product: LGA VideoDownloader
release_repo: legandrop/LGA_VideoDownloader
tech_changelog: ChangeLog.md
version_heading: "v{v}:"
platforms: [win, mac]
---
# What's new in LGA VideoDownloader
<!-- Editable while a version is unpublished. NOT append-only. Published versions are frozen. -->

## Unreleased

## v1.000
- [new] VimeoDownloader ahora es LGA VideoDownloader, una app rehecha que baja de YouTube, Vimeo, SoundCloud, Instagram y la mayoría de los demás sitios de video y audio.
- [new][win] Una extensión para Chrome, Brave y Edge le manda a la app, con un clic, la página que estás mirando junto con tu sesión, así los videos privados o para miembros se bajan sin exportar cookies. En Settings está cómo instalarla.
- [new] Podés pegar varios links juntos, o cualquier texto que los tenga: cada link pasa a ser una tarjeta en la cola, con su progreso, su botón para cancelar y su reintento.
- [new] Podés elegir entre video MP4 o solo audio (M4A); la calidad que viene elegida, "Most compatible", da archivos H.264 que abren en cualquier editor.
- [improved] Los videos privados ya no piden usuario y contraseña: la app usa la sesión de un navegador donde ya entraste, o un archivo cookies.txt, y borra el usuario y la contraseña que haya guardado una versión anterior.
- [new] Cuando sale una versión nueva, la app muestra qué cambió y la instala por vos, o te la recuerda mañana o saltea esa versión; si la conexión falla al buscar o bajar la actualización, espera y vuelve a probar sola, y una descarga cortada sigue desde donde quedó.
- [new] Los avisos de actualización muestran qué hay de nuevo en cada versión, y Help tiene la historia completa. La app busca una versión nueva cada algunas horas mientras está abierta, no solo al arrancar.
- [new] yt-dlp y las demás herramientas de descarga se actualizan solas en segundo plano, así los sitios que cambian siguen andando sin reinstalar; si la conexión falla, la app espera y vuelve a probar sola, y lo mismo hacen las descargas cuando se corta la conexión.
- [improved] Los errores dicen qué pasó y cómo arreglarlo, por ejemplo cuando un video pide iniciar sesión o un sitio no está soportado; las transmisiones en vivo y los estrenos programados se saltean con un mensaje claro.
- [improved] Al cancelar una descarga, o al cerrar la app, se borran sus archivos a medio bajar.
- [new] El log queda siempre a la vista abajo, con filtros y un botón Copy.
- [improved] La parte de arriba de la ventana es como en las otras apps LGA: el nombre de la app a la izquierda y Settings y Help a la derecha, sin la pestaña DOWNLOADS sola.
- [new] Settings tiene la búsqueda de actualizaciones, cómo instalar la extensión del navegador y las versiones de las herramientas de descarga; Help muestra la versión de la app, las novedades y los créditos.
- [improved] La ventana entra en pantallas chicas de notebook sin controles encimados.
- [improved][win] Al instalar se borra el VimeoDownloader viejo y se conserva tu carpeta de descargas, y todo lo que la app baja para funcionar queda dentro de su carpeta de instalación, así que desinstalarla lo borra todo.
- [improved][mac] La app ahora se llama "LGA Video Downloader", se instala desde un DMG firmado y verificado por Apple que se abre sin pasar por la Terminal, corre nativa en Apple Silicon y no necesita nada más instalado en la Mac.
- [improved] Ícono nuevo, en la línea de las otras apps LGA.
- [improved] La app ahora se publica compilada en modo optimizado: las versiones anteriores salían en modo de depuración, que es más lento.
- [fixed][mac] Si la app tiene que bajar ffmpeg y la conexión falla, ya no se rinde al primer intento: espera y vuelve a probar sola.

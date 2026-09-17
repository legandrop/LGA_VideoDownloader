cmake_minimum_required(VERSION 3.16)
project(VideoDownloader VERSION 0.95 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configuración específica para macOS - Compatibilidad de versiones
if(APPLE)
    # Establecer la versión mínima de macOS soportada (macOS 11.0 Big Sur)
    # Actualizado para mejor compatibilidad con versiones nuevas como Sequoia
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OS X deployment version")
    
    # Configurar arquitecturas - soporte para Intel y Apple Silicon
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
        # Si estamos compilando en Apple Silicon, crear binario universal
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for OS X")
    else()
        # Si estamos compilando en Intel, solo Intel por ahora
        set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build architectures for OS X")
    endif()
    
    # Configuraciones adicionales para macOS
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
endif()

# Configuración de Debug
set(CMAKE_BUILD_TYPE Debug)
if(WIN32)
    if(MSVC)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi")
    else()
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
    endif()
elseif(APPLE)
    # Configuración de debug para macOS
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Configuración para hacer la aplicación portable
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install)

if(WIN32)
    set(CMAKE_PREFIX_PATH "C:/Qt/6.8.2/mingw_64")
else()
    set(CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.8.2/macos")
endif()

# Encontrar paquetes de Qt
find_package(Qt6 COMPONENTS Core Gui Widgets Network REQUIRED)

# Encontrar paquetes específicos de Windows
if(WIN32)
    find_package(Qt6 COMPONENTS WinExtras QUIET)
    if(NOT Qt6WinExtras_FOUND)
        # En Qt6, WinExtras se movió a Core
        message(STATUS "Qt6WinExtras no encontrado, usando funcionalidades integradas en Qt6::Core")
    endif()
endif()

# Recursos
set(RESOURCES
    resources/resources.qrc
)

# Configuración del icono para Windows
if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/LGA_VideoDownloader.ico")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows_icon.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/windows_icon.rc" @ONLY)
    set(RESOURCES ${RESOURCES} "${CMAKE_CURRENT_BINARY_DIR}/windows_icon.rc")
endif()

# Archivos fuente
set(SOURCES
    src/main.cpp
    src/ui/mainwindow.cpp
    src/ui/theme.cpp
    src/ui/uiwidgets.cpp
    src/ui/tabheader.cpp
    src/ui/addvideoscard.cpp
    src/ui/queueview.cpp
    src/ui/logview.cpp
    src/ui/helpdialog.cpp
    src/ui/videopassworddialog.cpp
    src/qa/uishot.cpp
    src/core/downloadqueue.cpp
    src/core/updateservice.cpp
    src/core/nativehost.cpp
    src/core/appinstance.cpp
    src/utils/apppaths.cpp
    src/utils/browserdetect.cpp
    src/utils/hostregistration.cpp
    src/utils/sessioncookies.cpp
    src/utils/linkparser.cpp
    src/utils/toolsmanager.cpp
    src/utils/toolsupdater.cpp
    src/utils/versioncompare.cpp
    src/utils/LgaRegistry.cpp
)

# Archivos de cabecera
set(HEADERS
    include/videodownloader/mainwindow.h
    include/videodownloader/videopassworddialog.h
    include/videodownloader/theme.h
    include/videodownloader/uiwidgets.h
    include/videodownloader/tabheader.h
    include/videodownloader/addvideoscard.h
    include/videodownloader/queueview.h
    include/videodownloader/logview.h
    include/videodownloader/helpdialog.h
    include/videodownloader/uishot.h
    include/videodownloader/apppaths.h
    include/videodownloader/browserdetect.h
    include/videodownloader/linkparser.h
    include/videodownloader/loglevel.h
    include/videodownloader/toolsmanager.h
    include/videodownloader/toolsupdater.h
    include/videodownloader/updateservice.h
    include/videodownloader/updateurls.h
    include/videodownloader/versioncompare.h
    include/videodownloader/LgaRegistry.h
    include/videodownloader/downloadqueue.h
    include/videodownloader/downloaditem.h
    include/videodownloader/nativehost.h
    include/videodownloader/appinstance.h
    include/videodownloader/hostregistration.h
    include/videodownloader/sessioncookies.h
)

# Extension de navegador: el ID fijo sale de extension/EXTENSION_ID.txt (unica fuente). El exe
# lo usa para aceptar solo ese origen y para escribir el JSON del host.
set(VD_EXTENSION_ID_FILE "${CMAKE_CURRENT_SOURCE_DIR}/extension/EXTENSION_ID.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VD_EXTENSION_ID_FILE}")
file(READ "${VD_EXTENSION_ID_FILE}" VD_EXTENSION_ID)
string(STRIP "${VD_EXTENSION_ID}" VD_EXTENSION_ID)
if(NOT VD_EXTENSION_ID MATCHES "^[a-p]+$")
    message(FATAL_ERROR "extension/EXTENSION_ID.txt no tiene un ID de extension valido: '${VD_EXTENSION_ID}'")
endif()
string(LENGTH "${VD_EXTENSION_ID}" VD_EXTENSION_ID_LENGTH)
if(NOT VD_EXTENSION_ID_LENGTH EQUAL 32)
    message(FATAL_ERROR "extension/EXTENSION_ID.txt debe tener 32 caracteres a-p")
endif()
if(WIN32)
    # JSON del host para el deploy de Windows (path relativo al exe). La app lo repara al
    # arrancar si falta o no coincide.
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/native_host_windows.json.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/com.lga.videodownloader.json" @ONLY NEWLINE_STYLE UNIX)
endif()

# Archivos de UI (comentado por ahora, no usamos archivos .ui)
# set(UI_FILES
#     src/ui/mainwindow.ui
# )

# Crear el ejecutable
add_executable(${PROJECT_NAME} 
    ${SOURCES} 
    ${HEADERS} 
    ${RESOURCES}
)

# Vincular con las bibliotecas de Qt
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

# Vincular con bibliotecas específicas de Windows
if(WIN32)
    if(Qt6WinExtras_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::WinExtras)
    endif()
endif()

# Incluir directorios
# Version al C++: fuente unica de verdad en el project() de arriba.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VIDEODOWNLOADER_VERSION="${PROJECT_VERSION}"
    # Arboles de DESARROLLO de este proyecto (build y repo). Los usa LgaRegistry para NO
    # registrarse cuando el binario corre desde ahi: si no, el build pisa en el registro
    # compartido a la copia instalada, que es la que le sirve a las otras apps LGA. El repo
    # entra ademas del build porque el `deploy/` es hermano del build y no hijo. Son la
    # segunda red: LgaRegistry ya descarta por el NOMBRE de la carpeta contenedora
    # (`build`, `deploy`), y estas rutas agarran lo que el nombre no ve (un `out/`, por
    # ejemplo). Salen de CMake, asi que valen igual en Windows y macOS.
    LGA_BUILD_TREE_DIR="${CMAKE_BINARY_DIR}"
    LGA_SOURCE_TREE_DIR="${CMAKE_SOURCE_DIR}"
    LGA_EXTENSION_ID="${VD_EXTENSION_ID}"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Configuración para Windows
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# Configuración para macOS
if(APPLE)
    # Configurar como aplicación de macOS
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        # Convencion LGA: el nombre visible arranca con "LGA" para que todas las apps queden
        # juntas en /Applications, donde Finder ordena por el nombre de ARCHIVO del bundle (no
        # por CFBundleName). OUTPUT_NAME define el .app Y el ejecutable de adentro: en un target
        # MACOSX_BUNDLE no se pueden separar. El nombre de ARCHIVO del .zip/.dmg es otra cosa
        # (LGA_Video_Downloader_Mac_vX): va sin espacios porque viaja por URL en los releases.
        OUTPUT_NAME "LGA Video Downloader"
        MACOSX_BUNDLE_BUNDLE_NAME "LGA Video Downloader"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_IDENTIFIER "com.lga.videodownloader"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.lga.videodownloader"
        MACOSX_BUNDLE_EXECUTABLE_NAME "LGA Video Downloader"
        MACOSX_BUNDLE_COPYRIGHT "© 2024 LGA. Todos los derechos reservados."
        # Configurar versiones de macOS soportadas
        MACOSX_BUNDLE_INFO_STRING "LGA Video Downloader ${PROJECT_VERSION}, Compatible con macOS 10.15+"
    )
    
    # Icono de macOS: pipeline Icon Composer. El icono se disena en
    # resources/icons/Alta/<Nombre>.icon y se compila con actool. Instrucciones
    # completas: seccion "5.2 Icon Composer" de ../LGA_IconLab/docs/Doc_Iconos_App.md
    #
    # Van DOS recursos al bundle y hacen falta los dos:
    #   Assets.car    el icono moderno completo, que es lo que consume macOS 26 via
    #                 CFBundleIconName. Sin esto no hay tratamiento moderno.
    #   AppIcon.icns  fallback de compatibilidad, via CFBundleIconFile. Se genera con
    #                 --standalone-icon-behavior all para que traiga TODOS los tamanos
    #                 (16..1024); por defecto actool solo mete un subconjunto chico que
    #                 llega hasta 256 px.
    # NO convertir el .icon a un .icns suelto descartando el Assets.car: eso pierde el
    # comportamiento moderno y reaparece el problema de tamano en Cmd+Tab.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/AppIcon.icns")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
        )
        set(VD_MACOS_ICON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/AppIcon.icns")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/Assets.car")
            list(APPEND VD_MACOS_ICON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/Assets.car")
        else()
            message(WARNING "Falta resources/icons/Assets.car: el icono va a caer al .icns "
                            "de compatibilidad y se pierde el icono moderno.")
        endif()
        set_source_files_properties(${VD_MACOS_ICON_FILES}
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(${PROJECT_NAME} PRIVATE ${VD_MACOS_ICON_FILES})
    endif()

    # Extension de navegador dentro del bundle (no verificado en Mac). Al arrancar, la app la
    # copia a Application Support, que es la carpeta que se carga con "Load unpacked".
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/extension"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/extension"
                "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/extension"
        COMMENT "Copiando la extension de navegador al bundle")
endif()

# Instalación
if(WIN32)
    # Instalación para Windows
    install(TARGETS ${PROJECT_NAME} DESTINATION bin)
    install(FILES $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> DESTINATION bin)
    
elseif(APPLE)
    # Instalación para macOS
    install(TARGETS ${PROJECT_NAME} DESTINATION .)
    
else()
    # Instalación para Linux (si es necesario en el futuro)
    install(TARGETS ${PROJECT_NAME} DESTINATION bin)
endif()

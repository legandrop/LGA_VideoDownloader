# Auto-update aislado como biblioteca interna: concentra red, disco, validacion y dialogos.
# Incluye las notas para el usuario (LgaWhatsNew): bajarlas, verificarlas y armarlas en HTML.
# La app aporta su identidad, repositorio publico y caminos de cierre/configuracion.
qt_add_library(LGAUpdate STATIC
    src/LgaUpdateService.cpp
    src/LgaVersionCompare.cpp
    src/LgaWhatsNew.cpp
    include/lgaupdate/LgaUpdateService.h
    include/lgaupdate/LgaVersionCompare.h
    include/lgaupdate/LgaWhatsNew.h
)

target_include_directories(LGAUpdate
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(LGAUpdate
    PUBLIC
        Qt6::Core
        Qt6::Widgets
    PRIVATE
        Qt6::Network
)

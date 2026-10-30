set(GX_SOURCES
    ${GX_SOURCES}
    
    # Main lifecycle
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_load.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_loop.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_unload.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_update.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_parse.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_tests.c
    
    # Audio
    ${CMAKE_CURRENT_SOURCE_DIR}/audio.c
    
    # Camera
    ${CMAKE_CURRENT_SOURCE_DIR}/camera.c

    # Scene
    ${CMAKE_CURRENT_SOURCE_DIR}/scene/battle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/scene/scene.c
    ${CMAKE_CURRENT_SOURCE_DIR}/scene/title.c
    
    # Input
    ${CMAKE_CURRENT_SOURCE_DIR}/input.c
    
    # Common
    ${CMAKE_CURRENT_SOURCE_DIR}/common/heap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/enum.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/alloc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/random.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/utils.c
    
    # GUI — templates
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/templates/template_registry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/templates/templates.c
)

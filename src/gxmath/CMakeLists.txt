
file(GLOB sources ${CMAKE_CURRENT_SOURCE_DIR}/src/gxmath/*.c)
list(APPEND GX_SOURCES ${sources})

#opencode: SSE-optimized vector math (x86-64)
file(GLOB asm_sources ${CMAKE_CURRENT_SOURCE_DIR}/src/gxmath/*.s)
list(APPEND GX_SOURCES ${asm_sources})

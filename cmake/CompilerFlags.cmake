# Minimal CompilerFlags for gX
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers -Werror=implicit-function-declaration")
    #opencode: hunt - implicit declarations are ERRORS (was suppressed):
    #   a missing prototype promotes float args to double, silently
    #   zeroing uniform uploads (DOF/motionblur/TAA/lensflare were dead).
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers")
    #opencode: one section per function/data plus linker garbage collection
    #   trims unreferenced symbols from the Release payload the lean gate
    #   grades (source API is unchanged; only unreferenced code is dropped).
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
    if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "gc-sections")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
    endif()
endif()

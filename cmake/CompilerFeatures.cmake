# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: compiler capability probes. Warning flags and C11 features are checked
#   before use so the build never assumes a compiler supports them; the
#   resulting report is consumed by `make compiler-check`.

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckCSourceCompiles)
include(CheckIncludeFile)

set(GX_FEATURE_FLAGS_C "")
set(GX_FEATURE_FLAGS_CXX "")
set(GX_HAVE_WERROR_BOOL_COMPARE 0)
set(GX_HAVE_WERROR_BOOL_OPERATION 0)
set(GX_HAVE_WERROR_POINTER_TO_INT 0)
set(GX_HAVE_WERROR_INT_TO_POINTER 0)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(flag IN ITEMS
            "-Werror=bool-compare"
            "-Werror=bool-operation"
            "-Werror=pointer-to-int-cast"
            "-Werror=int-to-pointer-cast")
        string(MAKE_C_IDENTIFIER "GX_C_FLAG_${flag}" probe)
        check_c_compiler_flag("${flag}" ${probe})
        if(${probe})
            list(APPEND GX_FEATURE_FLAGS_C "${flag}")
        endif()
    endforeach()
    if(GX_C_FLAG__Werror_bool_compare)
        set(GX_HAVE_WERROR_BOOL_COMPARE 1)
    endif()
    if(GX_C_FLAG__Werror_bool_operation)
        set(GX_HAVE_WERROR_BOOL_OPERATION 1)
    endif()
    if(GX_C_FLAG__Werror_pointer_to_int_cast)
        set(GX_HAVE_WERROR_POINTER_TO_INT 1)
    endif()
    if(GX_C_FLAG__Werror_int_to_pointer_cast)
        set(GX_HAVE_WERROR_INT_TO_POINTER 1)
    endif()

    # Clang-only spelling; GCC rejects it and the probe records 0.
    check_c_compiler_flag("-Werror=tautological-constant-out-of-range-compare"
        GX_C_FLAG_TAUTOLOGICAL)
    if(GX_C_FLAG_TAUTOLOGICAL)
        list(APPEND GX_FEATURE_FLAGS_C
            "-Werror=tautological-constant-out-of-range-compare")
    endif()

    # C++ tools get the same high-signal errors where supported.
    foreach(flag IN ITEMS
            "-Werror=bool-compare"
            "-Werror=bool-operation"
            "-Werror=pointer-to-int-cast"
            "-Werror=int-to-pointer-cast")
        string(MAKE_C_IDENTIFIER "GX_CXX_FLAG_${flag}" probe)
        check_cxx_compiler_flag("${flag}" ${probe})
        if(${probe})
            list(APPEND GX_FEATURE_FLAGS_CXX "${flag}")
        endif()
    endforeach()
endif()

foreach(feature IN ITEMS
        GX_HAVE_C11_STATIC_ASSERT GX_HAVE_C11_THREAD_LOCAL GX_HAVE_C11_GENERIC
        GX_HAVE_C11_ATOMICS GX_HAVE_GNUC_ATOMIC GX_HAVE_STDATOMIC_H
        GX_HAVE_BUILTIN_EXPECT)
    unset(${feature} CACHE)
endforeach()

check_include_file("stdatomic.h" GX_HAVE_STDATOMIC_H)
check_c_source_compiles(
    "#include <stdatomic.h>\nint main(void){atomic_int x; atomic_init(&x, 0); return atomic_load(&x);}"
    GX_HAVE_C11_ATOMICS)
check_c_source_compiles(
    "#include <stdatomic.h>\nint main(void){return __atomic_load_n((int*)0, __ATOMIC_RELAXED);}"
    GX_HAVE_GNUC_ATOMIC)
check_c_source_compiles(
    "_Static_assert(1, \"probe\");\nint main(void){return 0;}"
    GX_HAVE_C11_STATIC_ASSERT)
check_c_source_compiles(
    "_Thread_local int value = 0;\nint main(void){return value;}"
    GX_HAVE_C11_THREAD_LOCAL)
check_c_source_compiles(
    "int main(void){return _Generic(1, int: 1, default: 0);}"
    GX_HAVE_C11_GENERIC)
check_c_source_compiles(
    "int main(void){return __builtin_expect(0, 0);}"
    GX_HAVE_BUILTIN_EXPECT)

foreach(feature IN ITEMS
        GX_HAVE_C11_STATIC_ASSERT GX_HAVE_C11_THREAD_LOCAL GX_HAVE_C11_GENERIC
        GX_HAVE_C11_ATOMICS GX_HAVE_GNUC_ATOMIC GX_HAVE_STDATOMIC_H
        GX_HAVE_BUILTIN_EXPECT)
    if(NOT ${feature})
        set(${feature} 0)
    endif()
endforeach()

set(GX_COMPILER_FEATURE_REPORT
    "${CMAKE_BINARY_DIR}/generated/compiler_features.txt")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
file(WRITE "${GX_COMPILER_FEATURE_REPORT}" "")

foreach(pair IN ITEMS
        "compiler_id=${CMAKE_C_COMPILER_ID}"
        "compiler_version=${CMAKE_C_COMPILER_VERSION}"
        "compiler_cxx_id=${CMAKE_CXX_COMPILER_ID}"
        "compiler_cxx_version=${CMAKE_CXX_COMPILER_VERSION}"
        "c11_static_assert=${GX_HAVE_C11_STATIC_ASSERT}"
        "c11_thread_local=${GX_HAVE_C11_THREAD_LOCAL}"
        "c11_generic=${GX_HAVE_C11_GENERIC}"
        "c11_atomics=${GX_HAVE_C11_ATOMICS}"
        "gnuc_atomic=${GX_HAVE_GNUC_ATOMIC}"
        "stdatomic=${GX_HAVE_STDATOMIC_H}"
        "builtin_expect=${GX_HAVE_BUILTIN_EXPECT}"
        "werror_bool_compare=${GX_HAVE_WERROR_BOOL_COMPARE}"
        "werror_bool_operation=${GX_HAVE_WERROR_BOOL_OPERATION}"
        "werror_pointer_to_int=${GX_HAVE_WERROR_POINTER_TO_INT}"
        "werror_int_to_pointer=${GX_HAVE_WERROR_INT_TO_POINTER}")
    string(REPLACE "=TRUE" "=1" pair "${pair}")
    string(REPLACE "=FALSE" "=0" pair "${pair}")
    file(APPEND "${GX_COMPILER_FEATURE_REPORT}" "${pair}\n")
endforeach()
file(APPEND "${GX_COMPILER_FEATURE_REPORT}"
    "c_flags=${GX_FEATURE_FLAGS_C}\n")
file(APPEND "${GX_COMPILER_FEATURE_REPORT}"
    "cxx_flags=${GX_FEATURE_FLAGS_CXX}\n")

string(REPLACE ";" " " GX_FEATURE_FLAGS_C_STR "${GX_FEATURE_FLAGS_C}")
string(REPLACE ";" " " GX_FEATURE_FLAGS_CXX_STR "${GX_FEATURE_FLAGS_CXX}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${GX_FEATURE_FLAGS_C_STR}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${GX_FEATURE_FLAGS_CXX_STR}")

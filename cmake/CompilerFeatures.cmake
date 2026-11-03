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

    # High-signal quality warnings: probed one by one, then promoted to
    # errors because the current tree is clean for all of them.
    # Clang accepts -Wimplicit-fallthrough but not GCC's level argument.
    check_c_compiler_flag("-Wimplicit-fallthrough" GX_HAVE_IMPLICIT_FALLTHROUGH_BASE)
    if(GX_HAVE_IMPLICIT_FALLTHROUGH_BASE)
        list(APPEND GX_FEATURE_FLAGS_C "-Wimplicit-fallthrough")
    endif()
    foreach(flag IN ITEMS
            "-Wimplicit-fallthrough=5"
            "-Wformat=2"
            "-Wundef"
            "-Wvla"
            "-Wpointer-arith"
            "-Wwrite-strings"
            "-Wold-style-definition"
            "-Wduplicated-cond"
            "-Wlogical-op")
        string(MAKE_C_IDENTIFIER "GX_QUALITY_${flag}" probe)
        check_c_compiler_flag("${flag}" ${probe})
        if(${probe})
            list(APPEND GX_FEATURE_FLAGS_C "${flag}")
        endif()
    endforeach()
    foreach(name IN ITEMS
            "implicit-fallthrough" "format" "undef" "vla" "pointer-arith"
            "write-strings" "old-style-definition" "duplicated-cond"
            "logical-op")
        string(MAKE_C_IDENTIFIER "GX_WERROR_${name}" probe)
        check_c_compiler_flag("-Werror=${name}" ${probe})
        if(${probe})
            list(APPEND GX_FEATURE_FLAGS_C "-Werror=${name}")
        endif()
    endforeach()

    # Clang-only spelling; GCC rejects it and the probe records 0.
    check_c_compiler_flag("-Werror=tautological-constant-out-of-range-compare"
        GX_C_FLAG_TAUTOLOGICAL)
    if(GX_C_FLAG_TAUTOLOGICAL)
        set(GX_HAVE_TAUTOLOGICAL 1)
    else()
        set(GX_HAVE_TAUTOLOGICAL 0)
    endif()
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
        GX_HAVE_BUILTIN_EXPECT GX_HAVE_ATTR_FORMAT GX_HAVE_ATTR_VISIBILITY
        GX_HAVE_ATTR_NOINLINE GX_HAVE_ATTR_ALIGNED GX_HAVE_BUILTIN_POPCOUNT
        GX_HAVE_BUILTIN_RETURN_ADDRESS GX_HAVE_NORETURN GX_HAVE_ALIGNAS
        GX_HAVE_HAS_INCLUDE GX_HAVE_SANITIZE_ADDRESS
        GX_HAVE_SANITIZE_UNDEFINED GX_HAVE_FNO_COMMON GX_HAVE_LTO
        GX_HAVE_COVERAGE GX_HAVE_STACK_PROTECTOR GX_C_FLAG_TAUTOLOGICAL)
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

check_c_source_compiles(
    "__attribute__((format(printf, 1, 2))) void probe(const char *fmt, ...);\nint main(void){return 0;}"
    GX_HAVE_ATTR_FORMAT)
check_c_source_compiles(
    "__attribute__((visibility(\"default\"))) int probe(void);\nint main(void){return 0;}"
    GX_HAVE_ATTR_VISIBILITY)
check_c_source_compiles(
    "__attribute__((noinline)) static int probe(void){return 0;}\nint main(void){return probe();}"
    GX_HAVE_ATTR_NOINLINE)
check_c_source_compiles(
    "__attribute__((aligned(16))) static int probe[4];\nint main(void){return probe[0];}"
    GX_HAVE_ATTR_ALIGNED)
check_c_source_compiles(
    "int main(void){return __builtin_popcount(3u);}"
    GX_HAVE_BUILTIN_POPCOUNT)
check_c_source_compiles(
    "int main(void){return (int)(__builtin_return_address(0) != 0);}"
    GX_HAVE_BUILTIN_RETURN_ADDRESS)
check_c_source_compiles(
    "_Noreturn void probe(void);\nvoid probe(void){for(;;){}}\nint main(void){return 0;}"
    GX_HAVE_NORETURN)
check_c_source_compiles(
    "_Alignas(16) static int probe[4];\nint main(void){return probe[0];}"
    GX_HAVE_ALIGNAS)
check_c_source_compiles(
    "#if !defined(__has_include)\n#error no has_include\n#endif\nint main(void){return 0;}"
    GX_HAVE_HAS_INCLUDE)
# Sanitizer/coverage runtimes link through the driver; check both phases.
set(GX_SAVED_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
set(GX_SAVED_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
foreach(pair IN ITEMS "address:-fsanitize=address" "undefined:-fsanitize=undefined")
    string(REPLACE ":" ";" parts "${pair}")
    list(GET parts 0 name)
    list(GET parts 1 flag)
    set(CMAKE_REQUIRED_FLAGS "${flag}")
    set(CMAKE_EXE_LINKER_FLAGS "${GX_SAVED_EXE_LINKER_FLAGS} ${flag}")
    string(TOUPPER "${name}" upper)
    check_c_source_compiles("int main(void){return 0;}" GX_HAVE_SANITIZE_${upper})
endforeach()
set(CMAKE_REQUIRED_FLAGS "--coverage")
set(CMAKE_EXE_LINKER_FLAGS "${GX_SAVED_EXE_LINKER_FLAGS} --coverage")
check_c_source_compiles("int main(void){return 0;}" GX_HAVE_COVERAGE)
set(CMAKE_REQUIRED_FLAGS "${GX_SAVED_REQUIRED_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${GX_SAVED_EXE_LINKER_FLAGS}")
check_c_compiler_flag("-fno-common" GX_HAVE_FNO_COMMON)
check_c_compiler_flag("-flto" GX_HAVE_LTO)
check_c_compiler_flag("-fstack-protector-strong" GX_HAVE_STACK_PROTECTOR)
if(GX_HAVE_FNO_COMMON)
    list(APPEND GX_FEATURE_FLAGS_C "-fno-common")
endif()

foreach(feature IN ITEMS
        GX_HAVE_C11_STATIC_ASSERT GX_HAVE_C11_THREAD_LOCAL GX_HAVE_C11_GENERIC
        GX_HAVE_C11_ATOMICS GX_HAVE_GNUC_ATOMIC GX_HAVE_STDATOMIC_H
        GX_HAVE_BUILTIN_EXPECT GX_HAVE_ATTR_FORMAT GX_HAVE_ATTR_VISIBILITY
        GX_HAVE_ATTR_NOINLINE GX_HAVE_ATTR_ALIGNED GX_HAVE_BUILTIN_POPCOUNT
        GX_HAVE_BUILTIN_RETURN_ADDRESS GX_HAVE_NORETURN GX_HAVE_ALIGNAS
        GX_HAVE_HAS_INCLUDE GX_HAVE_SANITIZE_ADDRESS GX_HAVE_SANITIZE_UNDEFINED
        GX_HAVE_FNO_COMMON GX_HAVE_LTO GX_HAVE_COVERAGE GX_HAVE_STACK_PROTECTOR)
    if(NOT ${feature})
        set(${feature} 0)
    endif()
endforeach()

set(GX_HAVE_QUALITY_COMMON 1)
if((NOT GX_QUALITY__Wimplicit_fallthrough_5
    AND NOT GX_HAVE_IMPLICIT_FALLTHROUGH_BASE)
   OR NOT GX_QUALITY__Wformat_2
   OR NOT GX_QUALITY__Wundef
   OR NOT GX_QUALITY__Wvla
   OR NOT GX_QUALITY__Wpointer_arith
   OR NOT GX_QUALITY__Wwrite_strings
   OR NOT GX_QUALITY__Wold_style_definition)
    set(GX_HAVE_QUALITY_COMMON 0)
endif()
set(GX_HAVE_QUALITY_GNU 1)
if(NOT GX_QUALITY__Wduplicated_cond
   OR NOT GX_QUALITY__Wlogical_op)
    set(GX_HAVE_QUALITY_GNU 0)
endif()

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
        "werror_int_to_pointer=${GX_HAVE_WERROR_INT_TO_POINTER}"
        "tautological=${GX_HAVE_TAUTOLOGICAL}"
        "quality_common=${GX_HAVE_QUALITY_COMMON}"
        "quality_gnu=${GX_HAVE_QUALITY_GNU}"
        "attr_format=${GX_HAVE_ATTR_FORMAT}"
        "attr_visibility=${GX_HAVE_ATTR_VISIBILITY}"
        "attr_noinline=${GX_HAVE_ATTR_NOINLINE}"
        "attr_aligned=${GX_HAVE_ATTR_ALIGNED}"
        "builtin_popcount=${GX_HAVE_BUILTIN_POPCOUNT}"
        "builtin_return_address=${GX_HAVE_BUILTIN_RETURN_ADDRESS}"
        "noreturn=${GX_HAVE_NORETURN}"
        "alignas=${GX_HAVE_ALIGNAS}"
        "has_include=${GX_HAVE_HAS_INCLUDE}"
        "sanitize_address=${GX_HAVE_SANITIZE_ADDRESS}"
        "sanitize_undefined=${GX_HAVE_SANITIZE_UNDEFINED}"
        "fno_common=${GX_HAVE_FNO_COMMON}"
        "lto=${GX_HAVE_LTO}"
        "coverage=${GX_HAVE_COVERAGE}"
        "stack_protector=${GX_HAVE_STACK_PROTECTOR}")
    string(REPLACE "=TRUE" "=1" pair "${pair}")
    string(REPLACE "=FALSE" "=0" pair "${pair}")
    file(APPEND "${GX_COMPILER_FEATURE_REPORT}" "${pair}\n")
endforeach()
file(APPEND "${GX_COMPILER_FEATURE_REPORT}"
    "c_flags=${GX_FEATURE_FLAGS_C}\n")
file(APPEND "${GX_COMPILER_FEATURE_REPORT}"
    "cxx_flags=${GX_FEATURE_FLAGS_CXX}\n")

# Feature flags are applied per gX target in CMakeLists.txt; third-party
# targets (raylib, AngelScript) keep their own warning budget.

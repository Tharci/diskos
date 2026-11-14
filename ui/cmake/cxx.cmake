# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# C++ for the modules in src/: C++23, strict warnings, and no exceptions or RTTI in module code
# (errors are values: std::expected). The legacy C keeps its own flags (cmake/mq_ui.cmake).

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DISKOS_WERROR "Fail the build on a C++ warning (CI sets this)" OFF)

add_library(diskos_cxx_warnings INTERFACE)
target_compile_options(diskos_cxx_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    # Conversions and casts: the target is 32-bit, so silent narrowing is a real bug class.
    -Wconversion -Wsign-conversion -Wdouble-promotion -Wold-style-cast -Wuseless-cast
    -Wcast-align=strict -Wcast-qual
    # Logic slips.
    -Wshadow -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wnull-dereference
    -Wimplicit-fallthrough -Wformat=2 -Wundef
    # Classes and declarations.
    -Wnon-virtual-dtor -Woverloaded-virtual -Wextra-semi -Wmissing-declarations
    $<$<BOOL:${DISKOS_WERROR}>:-Werror>
)

# diskos_module(<name> SOURCES <file>... [DEPENDS <target>...])
# The static library diskos::<name> from a folder in src/. Its headers are included by path from
# src/, e.g. "player/frame.hpp". DEPENDS are other modules (or libraries) its headers need.
function(diskos_module name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPENDS")
    add_library(diskos_${name} STATIC ${arg_SOURCES})
    add_library(diskos::${name} ALIAS diskos_${name})
    target_include_directories(diskos_${name} PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_compile_options(diskos_${name} PRIVATE -fno-exceptions -fno-rtti)
    target_link_libraries(diskos_${name} PUBLIC ${arg_DEPENDS} PRIVATE diskos_cxx_warnings)
    if(CMAKE_CROSSCOMPILING)
        target_link_libraries(diskos_${name} PRIVATE diskos_size_optimized)
    else()
        target_link_libraries(diskos_${name} PRIVATE diskos_reproducible)
    endif()
endfunction()

# diskos_test(<name> SOURCES <file>... DEPENDS <target>...)
# Catch2 tests, as the executable <name>_test with each TEST_CASE registered in CTest. Host builds
# only: on the device build this does nothing. Tests may use exceptions (Catch2 does).
function(diskos_test name)
    if(CMAKE_CROSSCOMPILING)
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPENDS")
    add_executable(${name}_test ${arg_SOURCES})
    target_link_libraries(${name}_test PRIVATE
        ${arg_DEPENDS} diskos_cxx_warnings Catch2::Catch2WithMain
    )
    # Listed when ctest runs, not after linking: a sanitized binary that cannot start on this
    # kernel (TSan, see src/README.md) then fails its tests rather than the build.
    catch_discover_tests(${name}_test DISCOVERY_MODE PRE_TEST)
endfunction()

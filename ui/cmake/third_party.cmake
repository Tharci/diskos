# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# Third-party code as third_party::<name> targets. Linking one is all a target needs: it carries
# the headers (as system headers, so our warnings stay out of them), the archives and the build
# step.

set(third_party_dir ${CMAKE_SOURCE_DIR}/third_party)

# diskos_native_dep(<name> [RECIPE <recipe>] [ARCHIVES <lib>...] [INCLUDES <dir>...]
#                   [DEPENDS <file>...])
# A static library built by tools/build-native-deps.py from its offline recipe in third_party/.
# RECIPE defaults to <name>, ARCHIVES (in link order) to <name>. DEPENDS lists the inputs whose
# change rebuilds the library; without it the library is built once.
function(diskos_native_dep name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "RECIPE" "ARCHIVES;INCLUDES;DEPENDS")
    if(NOT arg_RECIPE)
        set(arg_RECIPE ${name})
    endif()
    if(NOT arg_ARCHIVES)
        set(arg_ARCHIVES ${name})
    endif()
    list(TRANSFORM arg_ARCHIVES PREPEND ${DISKOS_DEP_OUT}/mipsel/lib)
    list(TRANSFORM arg_ARCHIVES APPEND .a)

    add_custom_command(
        OUTPUT ${arg_ARCHIVES}
        COMMAND ${CMAKE_COMMAND} -E env CROSS=${DISKOS_CROSS}
                ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-native-deps.py
                mipsel ${arg_RECIPE} --app ${CMAKE_SOURCE_DIR} --third-party ${third_party_dir}
                --output ${DISKOS_DEP_OUT}
        DEPENDS ${arg_DEPENDS}
        COMMENT "Building ${arg_RECIPE}"
        VERBATIM
    )
    add_custom_target(build_${name} DEPENDS ${arg_ARCHIVES})

    add_library(third_party_${name} INTERFACE)
    add_library(third_party::${name} ALIAS third_party_${name})
    target_include_directories(third_party_${name} SYSTEM INTERFACE ${arg_INCLUDES})
    target_link_libraries(third_party_${name} INTERFACE ${arg_ARCHIVES})
    add_dependencies(third_party_${name} build_${name})
endfunction()

# FreeType's configured headers come from its build, so whatever uses it waits for that.
diskos_native_dep(freetype
    INCLUDES ${third_party_dir}/freetype-2.13.3/include
             ${DISKOS_DEP_OUT}/src/freetype-2.13.3/include
)
file(GLOB_RECURSE mbedtls_inputs CONFIGURE_DEPENDS
    ${third_party_dir}/mbedtls-3.6.7/include/*
    ${third_party_dir}/mbedtls-3.6.7/library/*
    ${third_party_dir}/mbedconfig/*
)
diskos_native_dep(mbedtls
    ARCHIVES mbedtls mbedx509 mbedcrypto
    INCLUDES ${third_party_dir}/mbedtls-3.6.7/include ${third_party_dir}/mbedconfig
    DEPENDS ${mbedtls_inputs}
            ${third_party_dir}/build-mbedtls.sh
            ${third_party_dir}/vendor-build-common.sh
            ${CMAKE_SOURCE_DIR}/airplay_mbedtls_config.h
            ${CMAKE_SOURCE_DIR}/tools/build-native-deps.py
)
diskos_native_dep(monocypher
    INCLUDES ${third_party_dir}/monocypher-4.0.2/src
             ${third_party_dir}/monocypher-4.0.2/src/optional
)
diskos_native_dep(plist RECIPE libplist INCLUDES ${third_party_dir}/libplist-2.6.0/include)
diskos_native_dep(speexdsp
    INCLUDES ${third_party_dir}/speexdsp-1.2.1/include/speex ${third_party_dir}/speexconfig
)

# Header-only WAV/FLAC/MP3 decoders.
add_library(third_party_dr_libs INTERFACE)
add_library(third_party::dr_libs ALIAS third_party_dr_libs)
target_include_directories(third_party_dr_libs SYSTEM INTERFACE
    ${third_party_dir}/dr_libs-dfe8377631000664666519fdb83da193fd8037f4
)

# All of mq_ui's libraries, in link order: a static archive only resolves symbols needed by what
# precedes it.
add_library(third_party_mq_ui INTERFACE)
add_library(third_party::mq_ui ALIAS third_party_mq_ui)
target_link_libraries(third_party_mq_ui INTERFACE
    third_party::freetype third_party::mbedtls third_party::monocypher third_party::plist
    third_party::speexdsp third_party::dr_libs
)

# The SQLite amalgamation, with its own defines and none of the app's.
add_library(third_party_sqlite OBJECT ${CMAKE_SOURCE_DIR}/sqlite3.c)
target_compile_definitions(third_party_sqlite PRIVATE
    SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_MEMSTATUS=0 SQLITE_DQS=0
    SQLITE_OMIT_DEPRECATED
)
target_link_libraries(third_party_sqlite PRIVATE diskos_size_optimized)

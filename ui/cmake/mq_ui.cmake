# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# mq_ui: LVGL, the app sources, SQLite and the embedded audiodec image, linked statically.

# Sorted: the order the disk returns them in must not change the link order.
file(GLOB_RECURSE lvgl_sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/lvgl/src/*.c)
list(SORT lvgl_sources)

# Compiled as an object library so the link order is explicit (below): CMake would otherwise put
# the SQLite and helper-image objects first, and the order changes the binary.
add_library(diskos_app OBJECT ${lvgl_sources} ${legacy_sources})
set_target_properties(diskos_app PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_include_directories(diskos_app PRIVATE
    ${CMAKE_SOURCE_DIR}
    # Compiled with the app (in legacy_sources), so not system headers.
    ${third_party_dir}/nanopb-0.4.9.1
    ${third_party_dir}/cast
)
target_compile_definitions(diskos_app PRIVATE
    _GNU_SOURCE
    LV_CONF_INCLUDE_SIMPLE
    MBEDTLS_CONFIG_FILE="airplay_mbedtls_config.h"
    PB_VALIDATE_UTF8=1
    $<$<BOOL:${DISKOS_PROFILE}>:DISKOS_PROFILE>
    $<$<BOOL:${DISKOS_TEST_OUTPUTS}>:DISKOS_TEST_OUTPUTS>
    $<$<BOOL:${DISKOS_TEST_BT_PREPARE}>:DISKOS_TEST_BT_PREPARE>
)
target_compile_options(diskos_app PRIVATE -pthread -Wall)
target_link_libraries(diskos_app PRIVATE diskos_size_optimized third_party::mq_ui)
# Only the two objects that start the embedded helper are built for it.
set_source_files_properties(audio_decode.c airplay_sender.c
    PROPERTIES COMPILE_DEFINITIONS AUDIO_STOCK_EMBEDDED=1
)

add_executable(mq_ui
    $<TARGET_OBJECTS:diskos_app>
    $<TARGET_OBJECTS:third_party_sqlite>
    $<TARGET_OBJECTS:diskos_audiodec_image>
)
set_target_properties(mq_ui PROPERTIES LINKER_LANGUAGE C)
# --gc-sections drops every provably unreferenced function/data section at link (unused LVGL fonts,
# widgets, draw backends, SQLite functions) with no behaviour change: only unreachable symbols are
# removed.
target_link_options(mq_ui PRIVATE -static -pthread -Wl,--gc-sections)
target_link_libraries(mq_ui PRIVATE third_party::mq_ui m rt)

# Every on-screen colour is a theme role and every font a font role: tools/check_theme.py (also a
# pre-commit hook) runs before each link, over the app sources and every header outside lvgl/.
set(app_sources ${legacy_sources})
list(TRANSFORM app_sources PREPEND ${CMAKE_SOURCE_DIR}/)
file(GLOB_RECURSE app_headers CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/*.h)
list(FILTER app_headers EXCLUDE REGEX "^${CMAKE_SOURCE_DIR}/(lvgl|build)/")
add_custom_target(check_theme
    COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/tools/check_theme.py
            ${app_sources} ${app_headers}
    COMMENT "Checking theme colour and font roles"
)
add_dependencies(mq_ui check_theme)

# After the link: keep an unstripped twin for crash symbolication (not shipped), strip, prove the
# helper's bytes are carried intact, and write a small build manifest.
file(GLOB top_headers CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/*.h)  # with the sources: its src_md5
list(SORT top_headers)
add_custom_command(TARGET mq_ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mq_ui> $<TARGET_FILE:mq_ui>.debug
    COMMAND ${CMAKE_STRIP} $<TARGET_FILE:mq_ui>
    COMMAND sh -c "${Python3_EXECUTABLE} -I -B '${CMAKE_SOURCE_DIR}/tools/embed_audiodec.py' \
verify '$<TARGET_FILE:mq_ui>' '${audiodec_elf}' '${audiodec_image}' \
|| { rm -f '$<TARGET_FILE:mq_ui>'; exit 1; }"
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/write_manifest.sh $<TARGET_FILE:mq_ui> ${CMAKE_C_COMPILER}
            ${app_sources} ${top_headers}
    VERBATIM
)

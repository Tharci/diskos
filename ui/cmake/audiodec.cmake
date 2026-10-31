# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# diskos-audiodec: the helper that decodes through the stock firmware's own (dynamically linked)
# FFmpeg. It travels inside mq_ui (owner decision 2026-10-22): its finished ELF is the read-only
# .diskos_audiodec section, with a generated length and SHA-256. mq_ui never executes a pathname:
# before each start it writes a private copy, verifies it and executes that copy
# (audio_stock_process.h).

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env STOCK_AV_HEADERS=${STOCK_AV_HEADERS}
            sh ${CMAKE_SOURCE_DIR}/third_party/build-stock-audiodec.sh --verify-only
    RESULT_VARIABLE header_check
)
if(NOT header_check EQUAL 0)
    message(FATAL_ERROR "the FFmpeg header archive failed verification")
endif()

set(audiodec_elf ${CMAKE_BINARY_DIR}/diskos-audiodec)
set(audiodec_image ${CMAKE_BINARY_DIR}/audio_stock_image.raw)
add_custom_command(
    OUTPUT ${audiodec_elf}
    COMMAND ${CMAKE_COMMAND} -E env
            STOCK_APP=${CMAKE_SOURCE_DIR} STOCK_SYSROOT=${STOCK_SYSROOT}
            STOCK_AV_HEADERS=${STOCK_AV_HEADERS} STOCK_AUDIO_OUT=${DISKOS_DEP_OUT}/stock-audiodec
            sh ${CMAKE_SOURCE_DIR}/third_party/build-stock-audiodec.sh
    COMMAND ${CMAKE_COMMAND} -E copy
            ${DISKOS_DEP_OUT}/stock-audiodec/diskos-audiodec ${audiodec_elf}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/diskos_audiodec.c
            ${CMAKE_SOURCE_DIR}/tools/audio_stock_start.S
            ${CMAKE_SOURCE_DIR}/audio_stock_wire.h
            ${CMAKE_SOURCE_DIR}/third_party/build-stock-audiodec.sh
    COMMENT "Building diskos-audiodec against the stock sysroot"
)
add_custom_command(
    OUTPUT ${audiodec_image}
    COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_SOURCE_DIR}/tools/embed_audiodec.py image
            ${audiodec_elf} ${audiodec_image}
    DEPENDS ${audiodec_elf} ${CMAKE_SOURCE_DIR}/tools/embed_audiodec.py
)

# The section itself: audio_stock_image.S includes the image's bytes.
add_library(diskos_audiodec_image OBJECT audio_stock_image.S)
target_compile_definitions(diskos_audiodec_image PRIVATE
    AUDIO_STOCK_IMAGE_RAW="${audiodec_image}"
)
set_source_files_properties(audio_stock_image.S PROPERTIES OBJECT_DEPENDS ${audiodec_image})

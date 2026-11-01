# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# Static analysis of the C++ modules, with the device build's own compile commands, so it sees the
# real target (32-bit MIPS, musl, the cross GCC's libstdc++):
#   cmake --build --preset device -t tidy        clang-tidy, with the checks in src/.clang-tidy
#   cmake --build --preset device -t cppcheck    cppcheck
# With DISKOS_WERROR (CI) any finding fails the target. The legacy C is not checked.

find_program(RUN_CLANG_TIDY NAMES run-clang-tidy run-clang-tidy-19)
find_program(CPPCHECK cppcheck)

if(RUN_CLANG_TIDY)
    # The commands are GCC's, which finds its headers itself. Clang needs the target, the sysroot
    # and libstdc++'s directories (the c++ ones among GCC's implicit include directories) spelled
    # out, and skips GCC's own builtin headers for its own.
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -print-sysroot
        OUTPUT_VARIABLE cross_sysroot OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(libstdcxx_dirs ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
    list(FILTER libstdcxx_dirs INCLUDE REGEX "/c\\+\\+/")
    set(tidy_args
        -extra-arg-before=--target=mipsel-linux-musl
        -extra-arg-before=--sysroot=${cross_sysroot}
    )
    foreach(dir IN LISTS libstdcxx_dirs)
        list(APPEND tidy_args -extra-arg-before=-isystem${dir})
    endforeach()
    # GCC-only warning flags in the commands are not errors for clang.
    list(APPEND tidy_args -extra-arg=-Wno-unknown-warning-option)
    if(DISKOS_WERROR)
        list(APPEND tidy_args -warnings-as-errors=*)
    endif()

    add_custom_target(tidy
        COMMAND ${RUN_CLANG_TIDY} -quiet -p ${CMAKE_BINARY_DIR} ${tidy_args}
                # The modules' headers too (not the C headers in legacy/). Given here: clang-tidy 19
                # ignores HeaderFilterRegex in src/.clang-tidy.
                -header-filter=${PROJECT_SOURCE_DIR}/src/.*\\.hpp$
                "${PROJECT_SOURCE_DIR}/src/.*\\.cpp$"
        COMMENT "clang-tidy over src/"
        VERBATIM
    )
else()
    message(STATUS "run-clang-tidy not found: no tidy target")
endif()

if(CPPCHECK)
    # The commands' own include paths are used; the standard library comes from cppcheck's std.cfg.
    set(cppcheck_args
        --enable=warning,style,performance,portability --inline-suppr --quiet
        --suppress=missingIncludeSystem
    )
    if(DISKOS_WERROR)
        list(APPEND cppcheck_args --error-exitcode=1)
    endif()
    add_custom_target(cppcheck
        COMMAND ${CPPCHECK} --project=${CMAKE_BINARY_DIR}/compile_commands.json
                --file-filter=${PROJECT_SOURCE_DIR}/src/* ${cppcheck_args}
        COMMENT "cppcheck over src/"
        VERBATIM
    )
else()
    message(STATUS "cppcheck not found: no cppcheck target")
endif()

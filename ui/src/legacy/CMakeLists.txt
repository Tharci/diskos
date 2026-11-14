# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors

diskos_module(
    legacy
    SOURCES
        player_reports.cpp player_state.cpp player_controller.cpp ipc_command_sender.cpp
        player_link.cpp output.cpp
    DEPENDS diskos::player diskos::playback diskos::output diskos::platform
)
# The legacy C headers (ipc.h), for the C++ side of the bridge.
target_include_directories(diskos_legacy PRIVATE ${PROJECT_SOURCE_DIR})
diskos_test(legacy SOURCES player_reports_test.cpp player_state_test.cpp DEPENDS diskos::legacy)

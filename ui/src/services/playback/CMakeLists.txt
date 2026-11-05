# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors

diskos_module(
    playback
    SOURCES player_state.cpp player_controller.cpp
    DEPENDS diskos::player diskos::utils
)
diskos_test(
    playback
    SOURCES player_state_test.cpp player_controller_test.cpp
    DEPENDS diskos::playback
)

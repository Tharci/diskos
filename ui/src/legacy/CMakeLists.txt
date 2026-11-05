# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors

diskos_module(
    legacy
    SOURCES player_reports.cpp player_state.cpp
    DEPENDS diskos::player diskos::playback
)
diskos_test(legacy SOURCES player_reports_test.cpp player_state_test.cpp DEPENDS diskos::legacy)

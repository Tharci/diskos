# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors

diskos_module(
    player
    SOURCES frame.cpp reports.cpp track_report.cpp commands.cpp player_inbox.cpp
    DEPENDS diskos::utils
)
diskos_test(
    player
    SOURCES
        frame_test.cpp reports_test.cpp track_report_test.cpp commands_test.cpp
        player_inbox_test.cpp report_decoder_test.cpp
    DEPENDS diskos::player
)

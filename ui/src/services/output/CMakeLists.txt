# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors

diskos_module(
    output
    SOURCES
        bluetooth_address.cpp route_plan.cpp player_readiness.cpp transition.cpp output_router.cpp
    DEPENDS diskos::player diskos::playback
)
diskos_test(
    output
    SOURCES
        bluetooth_address_test.cpp route_plan_test.cpp player_readiness_test.cpp
        transition_test.cpp output_router_test.cpp
    DEPENDS diskos::output
)

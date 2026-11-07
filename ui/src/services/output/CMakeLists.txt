# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors

diskos_module(
    output
    SOURCES bluetooth_address.cpp route_plan.cpp
    DEPENDS diskos::player
)
diskos_test(
    output
    SOURCES bluetooth_address_test.cpp route_plan_test.cpp
    DEPENDS diskos::output
)

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstddef>

#include "player/report_listener.hpp"

namespace diskos::legacy {

/**
 * Hand everything the link received since the last call to the legacy C listener given to
 * diskos_link_start() (player_link.h), then to `services`, decoded, at `now`: each event goes to
 * both before the next. Does nothing before diskos_link_start(). Main loop only.
 * Returns how many frames were dropped since the last call (PlayerLink::deliver()).
 */
std::size_t deliver_player(
        player::ReportListener& services, player::ReportListener::Clock::time_point now
);

}  // namespace diskos::legacy

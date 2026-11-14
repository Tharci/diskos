// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <chrono>

#include "player/player_instance.hpp"
#include "player/reports.hpp"

namespace diskos::player {

/**
 * Hears the stock player's reports, decoded, on the thread that asks for them
 * (PlayerLink::deliver()): how services hear the player, as CommandSender is how they command it.
 */
class ReportListener {
public:
    using Clock = std::chrono::steady_clock;

    ReportListener() = default;
    ReportListener(const ReportListener&) = delete;
    ReportListener(ReportListener&&) = delete;
    ReportListener& operator=(const ReportListener&) = delete;
    ReportListener& operator=(ReportListener&&) = delete;
    virtual ~ReportListener() = default;

    /** A new player instance connected, at `now`. Comes before any of its reports. */
    virtual void on_connected(PlayerInstance instance, Clock::time_point now) = 0;

    /** One report from the current instance, at `now`. Its views last only during the call. */
    virtual void on_report(const Report& report, Clock::time_point now) = 0;
};

}  // namespace diskos::player

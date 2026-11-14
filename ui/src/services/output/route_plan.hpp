// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <chrono>
#include <optional>
#include <variant>
#include <vector>

#include "player/commands.hpp"
#include "services/output/output.hpp"

/**
 * The command sequences that move the player's audio to another output.
 *
 * The player's 0666 handler (V2.57 mq_player 0x4f1294) closes the player and re-initialises the
 * output, which leaves it in NO_WORK_MODE: every sequence ends with 0657 8 to set LOCALPLAYER
 * again. Without it the player is dead.
 */
namespace diskos::output {

/** Send a command. */
struct SendStep {
    player::Command command;
};

/** Wait before the next step. */
struct WaitStep {
    std::chrono::milliseconds duration{};
};

using Step = std::variant<SendStep, WaitStep>;

/**
 * Plan the steps that move the player's audio from `from` (nullopt when unknown) to `to`. Each
 * sequence is complete in itself, so one from an unknown output is the same.
 *  - To the output already applied: no steps.
 *  - To the internal DAC or S/PDIF: 0666, 65 ms, 0642 0 (which also drops a USB host request),
 *    0657 8. These are the frames and the wait of the stock Working Mode screen (V2.57 mq_ui
 *    0x46bd28).
 *  - To USB audio: 0666 3, 0642 5, 0657 8 (stock 0x46bd90). Check that a USB DAC is plugged in
 *    before running it: without one, the player's USB host output plays into a silent sink
 *    (V2.57 0x47c040).
 *  - To a Bluetooth sink: 0666 6, 0642 0, 0657 8, 06c1, 0666 2, 0657 8, as captured from the stock
 *    UI. Routing to the internal DAC first avoids a player crash seen with 0666 2 alone.
 *  - From a Bluetooth sink to S/PDIF or USB audio: through the internal DAC first, the one way
 *    off Bluetooth that was seen working.
 */
[[nodiscard]] std::vector<Step> plan_route(const std::optional<Output>& from, const Output& to);

/**
 * Plan the frames stock mq_ui sends right before a local play from idle: 0666 to `output` (and
 * 0642 5 for USB audio), then 0657 8. An idle player drifts its output (to Bluetooth, out_dev 2)
 * and drops its work mode, so a play from stopped otherwise fails: a ~15 s hang ("Failed to get
 * BT device MAC") or "work mode NULL" (legacy, device-confirmed 2026-09-01; also seen on plays of
 * the reserved playlist slot, list type 5, such as audiobooks). The player takes its
 * frames in order, so the play that follows gets both.
 * A Bluetooth sink needs none: the player keeps it. Returns no frames for one.
 */
[[nodiscard]] std::vector<player::Command> plan_play_preamble(const Output& output);

}  // namespace diskos::output

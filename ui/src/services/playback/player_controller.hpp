// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstdint>
#include <expected>

#include "player/command_sender.hpp"
#include "player/modes.hpp"
#include "services/playback/player_state.hpp"
#include "utils/synchronized.hpp"

namespace diskos::playback {

/**
 * Sends the user's playback commands to the stock player, and tells its PlayerState what a
 * command will do before the player reports it.
 * It keeps pointers to the sender and the state, which must outlive it. It holds the state's
 * lock only to read or update it, never while sending.
 */
class PlayerController {
public:
    using SendResult = std::expected<void, player::SendError>;

    PlayerController(player::CommandSender& sender, utils::Synchronized<PlayerState>& state)
        : sender_(&sender), state_(&state) {}

    /**
     * Play if paused, pause if playing: the player only has a toggle.
     * Returns SendError::Failed if it was not sent; the state is then left as it was.
     */
    [[nodiscard]] SendResult toggle(PlayerState::Clock::time_point now);

    /** Skip to the next track in the queue. Returns SendError::Failed if it was not sent. */
    [[nodiscard]] SendResult next();

    /** Go back to the previous track in the queue. Returns SendError::Failed if it was not sent. */
    [[nodiscard]] SendResult previous();

    /** Jump to `position_ms` in the current track. Returns SendError::Failed if it was not sent. */
    [[nodiscard]] SendResult seek(std::uint32_t position_ms);

    /** Set the volume. Returns SendError::Failed if it was not sent. */
    [[nodiscard]] SendResult set_volume(player::VolumeLevel level);

    /** Set how the player moves through its queue. Returns SendError::Failed if it was not sent. */
    [[nodiscard]] SendResult set_play_mode(player::PlayMode mode);

    /** Mark or unmark the current track as a favourite. Returns SendError::Failed if not sent. */
    [[nodiscard]] SendResult set_favourite(bool favourite);

private:
    player::CommandSender* sender_;
    utils::Synchronized<PlayerState>* state_;
};

}  // namespace diskos::playback

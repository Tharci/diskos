// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/playback/player_controller.hpp"

#include "player/commands.hpp"

namespace diskos::playback {

PlayerController::SendResult PlayerController::toggle(PlayerState::Clock::time_point now) {
    // Read before sending: once sent, a fast player may already have reported the outcome.
    const bool resumes =
            state_->with([](const PlayerState& state) { return state.is_resumable(); });
    SendResult sent = sender_->send(player::PlayPauseCommand{});
    if (sent && resumes) {
        // request_play() checks again, so a resume the player confirmed meanwhile stays Playing.
        state_->with([now](PlayerState& state) { state.request_play(now); });
    }
    return sent;
}

PlayerController::SendResult PlayerController::next() {
    return sender_->send(player::NextTrackCommand{});
}

PlayerController::SendResult PlayerController::previous() {
    return sender_->send(player::PreviousTrackCommand{});
}

PlayerController::SendResult PlayerController::seek(std::uint32_t position_ms) {
    return sender_->send(player::SeekCommand{.position_ms = position_ms});
}

PlayerController::SendResult PlayerController::set_volume(player::VolumeLevel level) {
    return sender_->send(player::SetVolumeCommand{.level = level});
}

PlayerController::SendResult PlayerController::set_play_mode(player::PlayMode mode) {
    return sender_->send(player::SetPlayModeCommand{.mode = mode});
}

PlayerController::SendResult PlayerController::set_favourite(bool favourite) {
    return sender_->send(player::SetFavouriteCommand{.favourite = favourite});
}

}  // namespace diskos::playback

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/playback/player_controller.hpp"

#include <utility>

#include "player/commands.hpp"

namespace diskos::playback {
namespace {

constexpr auto kToleranceMs =
        static_cast<std::uint64_t>(PlayerController::kRestoreTolerance.count());

/** What advance() needs to know about the player, read under one lock. */
struct Progress {
    std::optional<player::PlayerInstance> instance{};
    std::optional<Track> track{};
    bool playing = false;
    std::optional<std::uint32_t> position_ms{};
    std::optional<PlayerState::Clock::time_point> position_time{};
};

}  // namespace

PlayerController::SendResult PlayerController::toggle(Clock::time_point now) {
    return send_toggle(state_instance(), now);
}

PlayerController::SendResult PlayerController::next() {
    restore_.reset();
    return sender_->send(player::NextTrackCommand{}, state_instance());
}

PlayerController::SendResult PlayerController::previous() {
    restore_.reset();
    return sender_->send(player::PreviousTrackCommand{}, state_instance());
}

PlayerController::SendResult PlayerController::seek(std::uint32_t position_ms) {
    restore_.reset();
    return sender_->send(player::SeekCommand{.position_ms = position_ms}, state_instance());
}

PlayerController::SendResult PlayerController::set_volume(player::VolumeLevel level) {
    return sender_->send(player::SetVolumeCommand{.level = level}, state_instance());
}

PlayerController::SendResult PlayerController::set_play_mode(player::PlayMode mode) {
    return sender_->send(player::SetPlayModeCommand{.mode = mode}, state_instance());
}

PlayerController::SendResult PlayerController::set_favourite(bool favourite) {
    return sender_->send(player::SetFavouriteCommand{.favourite = favourite}, state_instance());
}

std::optional<PlaybackSnapshot> PlayerController::snapshot() const {
    return state_->with([](const PlayerState& state) -> std::optional<PlaybackSnapshot> {
        if (!state.track()) {
            return std::nullopt;
        }
        return PlaybackSnapshot{
                .track = *state.track(),
                .position_ms = state.position_ms().value_or(0),
                .playing = state.is_playing(),
        };
    });
}

PlayerController::SendResult PlayerController::pause(Clock::time_point now) {
    const auto [playing, instance] = state_->with([](const PlayerState& state) {
        return std::pair{state.is_playing(), state.instance()};
    });
    if (!playing) {
        return {};
    }
    return send_toggle(instance, now);
}

void PlayerController::note_output_reset(Clock::time_point now) {
    state_->with([now](PlayerState& state) { state.note_output_reset(now); });
}

PlayerController::SendResult PlayerController::restore(
        const PlaybackSnapshot& snapshot, Resume resume, Clock::time_point now
) {
    const std::optional<player::PlayerInstance> instance = state_instance();
    restore_.reset();
    if (snapshot.position_ms >= kToleranceMs) {
        restore_ = PendingRestore{
                .track = snapshot.track,
                .position_ms = snapshot.position_ms,
                .instance = instance,
        };
    }
    if (resume == Resume::OnNextPlay || !snapshot.playing) {
        return {};
    }
    SendResult sent = send_toggle(instance, now);
    if (!sent && sent.error() == player::SendError::WrongInstance) {
        restore_.reset();
    }
    return sent;
}

void PlayerController::advance(Clock::time_point now) {
    if (!restore_) {
        return;
    }
    PendingRestore& pending = *restore_;
    const Progress progress = state_->with([](const PlayerState& state) {
        return Progress{
                .instance = state.instance(),
                .track = state.track(),
                .playing = state.play_state() == PlayState::Playing,
                .position_ms = state.position_ms(),
                .position_time = state.position_time(),
        };
    });
    if (progress.instance != pending.instance
        || (progress.track && !same_track(*progress.track, pending.track))) {
        restore_.reset();  // Another player or another track: the restore is moot
        return;
    }
    if (!progress.track || !progress.playing) {
        return;  // A seek before the track plays is ignored
    }
    if (pending.last_seek_time) {
        if (!progress.position_ms || !progress.position_time
            || *progress.position_time <= *pending.last_seek_time) {
            return;  // Wait for a position reported after the seek
        }
        const std::uint64_t reached = std::uint64_t{*progress.position_ms} + kToleranceMs;
        if (reached >= pending.position_ms || pending.seek_attempts >= kMaxRestoreSeeks) {
            restore_.reset();  // Landed, or given up
            return;
        }
    }
    if (!send_restore_seek(pending, now)) {
        restore_.reset();
    }
}

std::optional<player::PlayerInstance> PlayerController::state_instance() const {
    return state_->with([](const PlayerState& state) { return state.instance(); });
}

PlayerController::SendResult PlayerController::send_toggle(
        std::optional<player::PlayerInstance> instance, Clock::time_point now
) {
    // Read before sending: once sent, a fast player may already have reported the outcome.
    const bool resumes =
            state_->with([](const PlayerState& state) { return state.is_resumable(); });
    SendResult sent = sender_->send(player::PlayPauseCommand{}, instance);
    if (sent && resumes) {
        // request_play() checks again, so a resume the player confirmed meanwhile stays Playing.
        state_->with([now](PlayerState& state) { state.request_play(now); });
    }
    return sent;
}

bool PlayerController::send_restore_seek(PendingRestore& pending, Clock::time_point now) {
    const SendResult sent = sender_->send(
            player::SeekCommand{.position_ms = pending.position_ms}, pending.instance
    );
    ++pending.seek_attempts;
    if (sent) {
        pending.last_seek_time = now;
        return true;
    }
    return sent.error() != player::SendError::WrongInstance
           && pending.seek_attempts < kMaxRestoreSeeks;
}

}  // namespace diskos::playback

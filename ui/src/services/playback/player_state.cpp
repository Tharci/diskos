// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/playback/player_state.hpp"

#include <utility>
#include <variant>

namespace diskos::playback {
namespace {

using player::ReportedPlayState;

constexpr PlayState play_state_of(ReportedPlayState state) {
    switch (state) {
        case ReportedPlayState::Playing:
            return PlayState::Playing;
        case ReportedPlayState::Paused:
            return PlayState::Paused;
        case ReportedPlayState::Stopped:
            return PlayState::Stopped;
    }
    std::unreachable();
}

/** Check whether two optional tracks are the same track, or both none. */
bool same_track(const std::optional<Track>& first, const std::optional<Track>& second) {
    if (!first || !second) {
        return first.has_value() == second.has_value();
    }
    return same_track(*first, *second);
}

std::optional<Track> track_of(const player::TrackReport& report) {
    // Only AirPlay songs have no path.
    if (report.song.path.empty() && report.work_mode != player::TrackReport::kAirPlay) {
        return std::nullopt;
    }
    return Track{
            .song = report.song,
            .favourite = report.favourite,
            .queue_position = report.queue_position,
    };
}

}  // namespace

bool same_track(const Track& first, const Track& second) {
    const player::Song& one = first.song;
    const player::Song& other = second.song;
    const bool same_texts =
            one.title == other.title && one.artist == other.artist && one.album == other.album;
    return one.path == other.path && one.id == other.id && one.pos_id == other.pos_id
           && (!one.path.empty() || same_texts);
}

Changes PlayerState::apply_report(const player::Report& report, Clock::time_point now) {
    const PlayState before = play_state_;
    advance(now);
    Changes changes = update(report);
    if (std::holds_alternative<player::PositionReport>(report)) {
        position_time_ = now;
    }
    if (changes.track || changes.play_state || changes.position) {
        progress_ = now;
    }
    changes.play_state = play_state_ != before;
    return changes;
}

Changes PlayerState::request_play(Clock::time_point now) {
    Changes changes = advance(now);
    if (is_resumable()) {
        play_state_ = PlayState::Loading;
        progress_ = now;
        changes.play_state = true;
    }
    return changes;
}

Changes PlayerState::request_list(Clock::time_point now) {
    Changes changes = advance(now);
    changes.play_state = changes.play_state || play_state_ != PlayState::Loading;
    play_state_ = PlayState::Loading;
    progress_ = now;
    return changes;
}

Changes PlayerState::note_output_reset(Clock::time_point now) {
    const PlayState before = play_state_;
    Changes changes = advance(now);
    changes.position = position_ms_.has_value();
    forget_position();
    if (play_state_ != PlayState::Stopped) {
        play_state_ = PlayState::Stopped;
        progress_ = now;
    }
    changes.play_state = play_state_ != before;
    return changes;
}

Changes PlayerState::update(const player::Report& report) {
    return std::visit([this](const auto& typed) { return update(typed); }, report);
}

Changes PlayerState::advance(Clock::time_point now) {
    const std::optional<Clock::time_point> expiry = deadline();
    if (!expiry || now < *expiry) {
        return {};
    }
    play_state_ = PlayState::Unknown;
    return {.play_state = true};
}

std::optional<PlayerState::Clock::time_point> PlayerState::deadline() const noexcept {
    switch (play_state_) {
        case PlayState::Playing:
            return progress_ + kStale;
        case PlayState::Loading:
            return progress_ + kLoadTimeout;
        case PlayState::Unknown:
        case PlayState::Stopped:
        case PlayState::Paused:
            return std::nullopt;
    }
    return std::nullopt;
}

Changes PlayerState::update(const player::TrackReport& report) {
    std::optional<Track> reported = track_of(report);
    const bool is_new = !same_track(track_, reported);
    // A track report's state lags: a track that is about to play still reads Stopped (capture of
    // 2026-11-01: {"state":0} follows about a second later). Only a change seen from a known
    // state is a load; the first report after startup is taken as it is.
    const bool loading = reported && report.state == ReportedPlayState::Stopped
                         && play_state_ != PlayState::Unknown
                         && (is_new || play_state_ == PlayState::Loading);
    const PlayState next_state = loading ? PlayState::Loading : play_state_of(report.state);
    const Changes changes{
            .track = is_new,
            .details = !is_new && track_ != reported,
            .play_state = next_state != play_state_,
            .position = is_new && position_ms_.has_value(),
    };
    if (is_new) {
        forget_position();
    }
    track_ = std::move(reported);
    play_state_ = next_state;
    return changes;
}

Changes PlayerState::update(const player::PlayStateReport& report) {
    // While a track loads, the player can report Paused for the one it is leaving (capture of
    // 2026-11-01, playing an artist); only Playing or Stopped settle the new one.
    if (play_state_ == PlayState::Loading && report.state == ReportedPlayState::Paused) {
        return {};
    }
    const PlayState next_state = play_state_of(report.state);
    const Changes changes{.play_state = next_state != play_state_};
    play_state_ = next_state;
    return changes;
}

Changes PlayerState::update(const player::PositionReport& report) {
    // Positions only come while playing, so one says so when nothing else has: after a UI restart
    // the player can ignore 02020008 until playback changes.
    const PlayState next_state =
            play_state_ == PlayState::Unknown ? PlayState::Playing : play_state_;
    const Changes changes{
            .play_state = next_state != play_state_,
            .position = position_ms_ != report.position_ms,
    };
    play_state_ = next_state;
    position_ms_ = report.position_ms;
    return changes;
}

void PlayerState::forget_position() noexcept {
    position_ms_.reset();
    position_time_.reset();
}

Changes PlayerState::update(const player::VolumeReport& report) {
    const Changes changes{.volume = volume_ != report.level};
    volume_ = report.level;
    return changes;
}

}  // namespace diskos::playback

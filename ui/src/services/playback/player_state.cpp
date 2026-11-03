// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/playback/player_state.hpp"

#include <utility>
#include <variant>

namespace diskos::playback {
namespace {

using player::ReportedPlayState;

constexpr PlayState activity_of(ReportedPlayState state) {
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

/**
 * Check whether two tracks are the same file at the same queue row (or both none). An AirPlay
 * song has no path and its ids are 0, so it is told apart by its texts.
 */
bool same_track(const std::optional<Track>& first, const std::optional<Track>& second) {
    if (!first || !second) {
        return first.has_value() == second.has_value();
    }
    const player::Song& one = first->song;
    const player::Song& other = second->song;
    const bool same_texts =
            one.title == other.title && one.artist == other.artist && one.album == other.album;
    return one.path == other.path && one.id == other.id && one.pos_id == other.pos_id
           && (!one.path.empty() || same_texts);
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

Changes PlayerState::apply_report(const player::Report& report) {
    return std::visit([this](const auto& typed) { return update(typed); }, report);
}

Changes PlayerState::update(const player::TrackReport& report) {
    std::optional<Track> track = track_of(report);
    const bool is_new = !same_track(track_, track);
    // A track report's state lags: a track that is about to play still reads Stopped (capture of
    // 2026-11-01: {"state":0} follows about a second later). Only a change seen from a known
    // state is a load; the first report after startup is taken as it is.
    const bool loading = track && report.state == ReportedPlayState::Stopped
                         && activity_ != PlayState::Unknown
                         && (is_new || activity_ == PlayState::Loading);
    const PlayState activity = loading ? PlayState::Loading : activity_of(report.state);
    const Changes changes{
            .track = is_new,
            .details = !is_new && track_ != track,
            .activity = activity != activity_,
            .position = is_new && position_ms_.has_value(),
    };
    if (is_new) {
        position_ms_.reset();
    }
    track_ = std::move(track);
    activity_ = activity;
    return changes;
}

Changes PlayerState::update(const player::PlayStateReport& report) {
    // While a track loads, the player can report Paused for the one it is leaving (capture of
    // 2026-11-01, playing an artist); only Playing or Stopped settle the new one.
    if (activity_ == PlayState::Loading && report.state == ReportedPlayState::Paused) {
        return {};
    }
    const PlayState activity = activity_of(report.state);
    const Changes changes{.activity = activity != activity_};
    activity_ = activity;
    return changes;
}

Changes PlayerState::update(const player::PositionReport& report) {
    // Positions only come while playing, so one says so when nothing else has: after a UI restart
    // the player can ignore 02020008 until playback changes.
    const PlayState activity = activity_ == PlayState::Unknown ? PlayState::Playing : activity_;
    const Changes changes{
            .activity = activity != activity_,
            .position = position_ms_ != report.position_ms,
    };
    activity_ = activity;
    position_ms_ = report.position_ms;
    return changes;
}

Changes PlayerState::update(const player::VolumeReport& report) {
    const Changes changes{.volume = volume_ != report.level};
    volume_ = report.level;
    return changes;
}

}  // namespace diskos::playback

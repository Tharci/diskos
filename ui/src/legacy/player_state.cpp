// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/player_state.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "legacy/player_state.h"
#include "player/reports.hpp"
#include "services/playback/player_state.hpp"
#include "utils/synchronized.hpp"

namespace diskos::legacy {
namespace {

using playback::Changes;
using playback::PlayerState;
using player::Report;
using player::TrackReport;

/** What a frame that is not applied did: nothing. */
constexpr diskos_player_update kIgnored{
        .accepted = 0,
        .track = 0,
        .details = 0,
        .position = 0,
        .reported_state = -1,
        .full = 0,
};

/** Check whether a text fits a legacy buffer as a C string. */
bool fits(std::string_view text) {
    return text.size() <= DISKOS_PLAYER_TEXT_MAX && !text.contains('\0');
}

/** Check whether a report's title and path fit the legacy buffers (only track reports have them).
 */
bool fits(const Report& report) {
    const auto* track = std::get_if<TrackReport>(&report);
    return track == nullptr || (fits(track->song.title) && fits(track->song.path));
}

/** The same track report without its path, which PlayerState takes as no local track. */
Report without_path(const Report& report) {
    TrackReport track = std::get<TrackReport>(report);
    track.song.path.clear();
    return track;
}

/** The state an a202 reported, or -1 for any other report. */
int reported_state(const Report& report) {
    if (const auto* state = std::get_if<player::PlayStateReport>(&report)) {
        return static_cast<int>(state->state);
    }
    if (const auto* track = std::get_if<TrackReport>(&report)) {
        return static_cast<int>(track->state);
    }
    return -1;
}

/** The track as the legacy C reads it: its texts point into `track`. */
diskos_player_track legacy_track(const playback::Track& track) {
    const player::Song& song = track.song;
    const player::QueuePosition queue = track.queue_position.value_or(player::QueuePosition{});
    return {
            .title = song.title.c_str(),
            .artist = song.artist.c_str(),
            .album = song.album.c_str(),
            .path = song.path.c_str(),
            .song_id = song.id,
            .pos_id = song.pos_id,
            .duration_ms = song.duration_ms,
            .sample_rate_hz = song.sample_rate_hz,
            .favourite = track.favourite ? 1 : 0,
            .is_dsd = song.is_dsd ? 1 : 0,
            .is_cue = song.is_cue ? 1 : 0,
            .is_sacd = song.is_sacd ? 1 : 0,
            .queue_position = queue.position,
            .queue_count = queue.count,
    };
}

bool is_applied(const Report& report) {
    return std::holds_alternative<player::PositionReport>(report)
           || std::holds_alternative<player::PlayStateReport>(report)
           || std::holds_alternative<TrackReport>(report);
}

}  // namespace

utils::Synchronized<playback::PlayerState>& player_state(diskos_player_slot slot) {
    static utils::Synchronized<playback::PlayerState> local;
    static utils::Synchronized<playback::PlayerState> receiver;
    return slot == DISKOS_PLAYER_RECEIVER ? receiver : local;
}

}  // namespace diskos::legacy

extern "C" diskos_player_update diskos_player_apply(
        diskos_player_slot slot, const char* bytes, size_t size
) {
    using namespace diskos::legacy;
    const auto report = diskos::player::decode_report(std::string_view{bytes, size});
    if (!report || !is_applied(*report)) {
        return kIgnored;
    }
    const bool usable = slot == DISKOS_PLAYER_RECEIVER || fits(*report);
    const Report applied = usable ? *report : without_path(*report);
    const auto now = PlayerState::Clock::now();
    const Changes changes = player_state(slot).with([&applied, now](PlayerState& state) {
        return state.apply_report(applied, now);
    });
    return {
            .accepted = usable ? 1 : 0,
            .track = changes.track ? 1 : 0,
            .details = changes.details ? 1 : 0,
            .position = changes.position ? 1 : 0,
            .reported_state = reported_state(*report),
            .full = std::holds_alternative<TrackReport>(*report) ? 1 : 0,
    };
}

extern "C" int diskos_player_get_track(diskos_player_slot slot, diskos_player_track* track) {
    using namespace diskos::legacy;
    return player_state(slot).with([track](const PlayerState& state) {
        if (!state.track()) {
            return 0;
        }
        *track = legacy_track(*state.track());
        return 1;
    });
}

extern "C" int diskos_player_advance(diskos_player_slot slot) {
    using namespace diskos::legacy;
    const auto now = PlayerState::Clock::now();
    return player_state(slot).with([now](PlayerState& state) {
        return state.advance(now).play_state ? 1 : 0;
    });
}

extern "C" int diskos_player_is_playing(diskos_player_slot slot) {
    using namespace diskos::legacy;
    return player_state(slot).with([](const PlayerState& state) {
        return state.is_playing() ? 1 : 0;
    });
}

extern "C" int64_t diskos_player_position(diskos_player_slot slot) {
    using namespace diskos::legacy;
    return player_state(slot).with([](const PlayerState& state) -> int64_t {
        const std::optional<std::uint32_t> position = state.position_ms();
        return position ? static_cast<int64_t>(*position) : -1;
    });
}

extern "C" void diskos_player_reset(diskos_player_slot slot, unsigned instance) {
    using namespace diskos::legacy;
    const diskos::player::PlayerInstance player{instance};
    player_state(slot).with([player](PlayerState& state) { state = PlayerState{player}; });
}

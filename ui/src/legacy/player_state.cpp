// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/player_state.h"

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "player/reports.hpp"
#include "services/playback/player_state.hpp"

namespace diskos::legacy {
namespace {

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

playback::PlayerState& player_state(diskos_player_slot slot) {
    static playback::PlayerState local;
    static playback::PlayerState receiver;
    return slot == DISKOS_PLAYER_RECEIVER ? receiver : local;
}

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

bool is_applied(const Report& report) {
    return std::holds_alternative<player::PositionReport>(report)
           || std::holds_alternative<player::PlayStateReport>(report)
           || std::holds_alternative<TrackReport>(report);
}

}  // namespace
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
    const diskos::playback::Changes changes =
            player_state(slot).apply_report(usable ? *report : without_path(*report));
    return {
            .accepted = usable ? 1 : 0,
            .track = changes.track ? 1 : 0,
            .details = changes.details ? 1 : 0,
            .position = changes.position ? 1 : 0,
            .reported_state = reported_state(*report),
            .full = std::holds_alternative<diskos::player::TrackReport>(*report) ? 1 : 0,
    };
}

extern "C" int diskos_player_get_track(diskos_player_slot slot, diskos_player_track* track) {
    const std::optional<diskos::playback::Track>& current =
            diskos::legacy::player_state(slot).track();
    if (!current) {
        return 0;
    }
    const diskos::player::Song& song = current->song;
    const diskos::player::QueuePosition queue =
            current->queue_position.value_or(diskos::player::QueuePosition{});
    *track = {
            .title = song.title.c_str(),
            .artist = song.artist.c_str(),
            .album = song.album.c_str(),
            .path = song.path.c_str(),
            .song_id = song.id,
            .pos_id = song.pos_id,
            .duration_ms = song.duration_ms,
            .sample_rate_hz = song.sample_rate_hz,
            .favourite = current->favourite ? 1 : 0,
            .is_dsd = song.is_dsd ? 1 : 0,
            .is_cue = song.is_cue ? 1 : 0,
            .is_sacd = song.is_sacd ? 1 : 0,
            .queue_position = queue.position,
            .queue_count = queue.count,
    };
    return 1;
}

extern "C" int64_t diskos_player_position(diskos_player_slot slot) {
    const std::optional<std::uint32_t> position = diskos::legacy::player_state(slot).position_ms();
    return position ? static_cast<int64_t>(*position) : -1;
}

extern "C" void diskos_player_reset(diskos_player_slot slot) {
    diskos::legacy::player_state(slot) = diskos::playback::PlayerState{};
}

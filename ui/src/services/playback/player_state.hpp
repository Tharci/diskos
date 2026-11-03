// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstdint>
#include <optional>

#include "player/modes.hpp"
#include "player/reports.hpp"

namespace diskos::playback {

/** What the player is doing with the current track. */
enum class PlayState : std::uint8_t {
    Unknown,  // Nothing reported since startup or a player restart
    Stopped,  // Also a track that could not be opened
    Loading,  // A new track was reported; it has not started yet
    Playing,
    Paused,
};

/** The current track, as the player last reported it. */
struct Track {
    player::Song song{};
    bool favourite = false;
    std::optional<player::QueuePosition> queue_position{};

    friend bool operator==(const Track&, const Track&) = default;
};

/** What one report changed. */
struct Changes {
    bool track = false;    // A different track, or none any more
    bool details = false;  // The same track's other fields: its favourite, texts or queue position
    bool activity = false;
    bool position = false;
    bool volume = false;

    friend bool operator==(const Changes&, const Changes&) = default;
};

class PlayerState {
public:
    /**
     * Apply one report from the player and say what it changed.
     *  - A track report for another file or queue row is a new track, with no position yet. Once
     *    the activity is known, the Stopped it reads means Loading.
     *  - Under AirPlay, songs have no path: one with other texts is a new track.
     *  - A local track report whose song has no path clears the track.
     *  - While Loading, a Paused report changes nothing.
     *  - A position report means Playing while the activity is Unknown.
     *  - Reports about anything other than playback (battery, screen, ...) change nothing.
     */
    Changes apply_report(const player::Report& report);

    // Properties

    /** Get the current track, or nullopt if none was reported or it was cleared. */
    [[nodiscard]] const std::optional<Track>& track() const noexcept {
        return track_;
    }

    /** Get what the player is doing, Unknown until it reports. */
    [[nodiscard]] PlayState activity() const noexcept {
        return activity_;
    }

    /** Get the last reported position, or nullopt if none was reported since the track changed. */
    [[nodiscard]] std::optional<std::uint32_t> position_ms() const noexcept {
        return position_ms_;
    }

    /** Get the last reported volume, or nullopt if none was reported. */
    [[nodiscard]] std::optional<player::VolumeLevel> volume() const noexcept {
        return volume_;
    }

private:
    Changes update(const player::TrackReport& report);
    Changes update(const player::PlayStateReport& report);
    Changes update(const player::PositionReport& report);
    Changes update(const player::VolumeReport& report);

    static Changes update(const auto& /*other*/) {
        return {};
    }

    std::optional<Track> track_{};
    PlayState activity_ = PlayState::Unknown;
    std::optional<std::uint32_t> position_ms_{};
    std::optional<player::VolumeLevel> volume_{};
};

}  // namespace diskos::playback

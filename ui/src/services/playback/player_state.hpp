// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "player/modes.hpp"
#include "player/reports.hpp"

namespace diskos::playback {

/** What the player is doing with the current track. */
enum class PlayState : std::uint8_t {
    Unknown,  // Nothing reported since startup or a player restart, or the reports stopped coming
    Stopped,  // Also a track that could not be opened
    Loading,  // A new track was reported, or play was requested; it has not started yet
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
    using Clock = std::chrono::steady_clock;

    // Positions come every second while playing; the first one up to ~3 s after a Bluetooth start.
    static constexpr Clock::duration kStale = std::chrono::seconds{5};
    // A track that cannot be opened reports Stopped after ~3.5 s.
    static constexpr Clock::duration kLoadTimeout = std::chrono::seconds{6};

    /**
     * Apply one report from the player and report what it changed.
     *  - A track report for another file or queue row is a new track, with no position yet. Once
     *    the activity is known, the Stopped it reads means Loading.
     *  - Under AirPlay, songs have no path: one with other texts is a new track.
     *  - A local track report whose song has no path clears the track.
     *  - While Loading, a Paused report changes nothing.
     *  - A position report means Playing while the activity is Unknown.
     *  - Reports about anything other than playback (battery, screen, ...) change nothing.
     * It first advances to `now`, so the outcome does not depend on how often advance() is called.
     */
    Changes apply_report(const player::Report& report, Clock::time_point now);

    /**
     * Note that a command to play the current track was sent, and report what changed. It first
     * advances to `now`.
     *  - If is_resumable(), the activity becomes Loading until the player reports Playing. The
     *    player confirms a resume over Bluetooth only after ~1.5 s (~3 s on a silent player),
     *    reporting the track as Stopped in between; on the speaker within ~10 ms.
     *  - Otherwise nothing changes: the player is already playing or loading.
     */
    Changes request_play(Clock::time_point now);

    /**
     * Advance to `now` and report what changed. An activity whose reports stopped coming (a lost
     * frame, or local frames held back while AirPlay owns the player) expires at deadline():
     * Playing or Loading becomes Unknown. Call it at the deadline, or regularly.
     */
    Changes advance(Clock::time_point now);

    /**
     * Get when the activity expires unless the player shows progress (a new state or track, or
     * the position moving), or nullopt when it cannot expire:
     *  - Playing: kStale after the last progress.
     *  - Loading: kLoadTimeout after the last progress.
     */
    [[nodiscard]] std::optional<Clock::time_point> deadline() const noexcept;

    // Properties

    /** Get the current track, or nullopt if none was reported or it was cleared. */
    [[nodiscard]] const std::optional<Track>& track() const noexcept {
        return track_;
    }

    /** Get what the player is doing, Unknown until it reports. */
    [[nodiscard]] PlayState activity() const noexcept {
        return play_state_;
    }

    /** Check whether the player is playing a track, or loading one to play. */
    [[nodiscard]] bool is_playing() const noexcept {
        return play_state_ == PlayState::Playing || play_state_ == PlayState::Loading;
    }

    /** Check whether there is a track that is Paused or Stopped, which a toggle would play. */
    [[nodiscard]] bool is_resumable() const noexcept {
        return track_.has_value()
               && (play_state_ == PlayState::Paused || play_state_ == PlayState::Stopped);
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
    /** Apply whichever report `report` holds, through the overloads below. */
    Changes update(const player::Report& report);
    Changes update(const player::TrackReport& report);
    Changes update(const player::PlayStateReport& report);
    Changes update(const player::PositionReport& report);
    Changes update(const player::VolumeReport& report);

    static Changes update(const auto& /*other*/) {
        return {};
    }

    std::optional<Track> track_{};
    PlayState play_state_ = PlayState::Unknown;
    Clock::time_point progress_{};  // When play_state_ or the track changed, or the position moved
    std::optional<std::uint32_t> position_ms_{};
    std::optional<player::VolumeLevel> volume_{};
};

}  // namespace diskos::playback

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstdint>
#include <expected>
#include <optional>

#include "player/command_sender.hpp"
#include "services/output/output.hpp"
#include "services/playback/player_controller.hpp"
#include "services/playback/player_state.hpp"

/** What the output router works with: each is implemented outside this module. */
namespace diskos::output {

using Clock = playback::PlayerState::Clock;
using SendResult = std::expected<void, player::SendError>;

/** The audio devices the router checks before routing to them. */
class AudioDevices {
public:
    AudioDevices() = default;
    AudioDevices(const AudioDevices&) = delete;
    AudioDevices(AudioDevices&&) = delete;
    AudioDevices& operator=(const AudioDevices&) = delete;
    AudioDevices& operator=(AudioDevices&&) = delete;
    virtual ~AudioDevices() = default;

    /** Check whether a USB DAC is plugged in. */
    [[nodiscard]] virtual bool usb_dac_present() = 0;
};

/** Why another part of diskOS keeps the output from changing now. */
enum class Blocker : std::uint8_t {
    CardNotReady,  // The SD card is not ready for the player
    OtherSource,   // The player is not on local playback
    Audiobook,     // An audiobook session is running
    PlayStarting,  // A play was sent and has not started yet
    Casting,       // A Cast session owns playback
};

/** Whether the rest of diskOS lets the output change now. */
class Admission {
public:
    Admission() = default;
    Admission(const Admission&) = delete;
    Admission(Admission&&) = delete;
    Admission& operator=(const Admission&) = delete;
    Admission& operator=(Admission&&) = delete;
    virtual ~Admission() = default;

    /** Get what keeps the output from changing now, or nullopt if nothing does. */
    [[nodiscard]] virtual std::optional<Blocker> blocker() = 0;
};

/** Local playback, as an output change suspends and restores it (playback::PlayerController). */
class PlaybackControl {
public:
    PlaybackControl() = default;
    PlaybackControl(const PlaybackControl&) = delete;
    PlaybackControl(PlaybackControl&&) = delete;
    PlaybackControl& operator=(const PlaybackControl&) = delete;
    PlaybackControl& operator=(PlaybackControl&&) = delete;
    virtual ~PlaybackControl() = default;

    /** Get what the player is doing. */
    [[nodiscard]] virtual playback::PlayState play_state() = 0;

    /** Get what is playing, or nullopt if there is no track. */
    [[nodiscard]] virtual std::optional<playback::PlaybackSnapshot> snapshot() = 0;

    /** Pause, if playing or loading. Returns a SendError if it was not sent. */
    [[nodiscard]] virtual SendResult pause(Clock::time_point now) = 0;

    /** Note that the player re-initialised its output: a 0666 landed. */
    virtual void note_output_reset(Clock::time_point now) = 0;

    /** Restore `snapshot` after the output changed. Returns a SendError if its play was not sent.
     */
    [[nodiscard]] virtual SendResult restore(
            const playback::PlaybackSnapshot& snapshot,
            playback::Resume resume,
            Clock::time_point now
    ) = 0;
};

/** What the router keeps across a UI restart, for the player instance that is still running. */
struct StoredRoute {
    std::optional<Output> applied{};  // nullopt when unknown
    bool settled = false;             // The player was ready: its late start-up is over
};

/** Keeps the router's StoredRoute across a UI restart. */
class RouteStore {
public:
    RouteStore() = default;
    RouteStore(const RouteStore&) = delete;
    RouteStore(RouteStore&&) = delete;
    RouteStore& operator=(const RouteStore&) = delete;
    RouteStore& operator=(RouteStore&&) = delete;
    virtual ~RouteStore() = default;

    /** Get what was saved for the player running now, or nullopt if nothing was, for it. */
    [[nodiscard]] virtual std::optional<StoredRoute> load() = 0;

    /** Save `route` for the player running now. A failure is not reported: it only costs a wait. */
    virtual void save(const StoredRoute& route) = 0;
};

}  // namespace diskos::output

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "player/command_sender.hpp"
#include "player/player_instance.hpp"
#include "services/output/interfaces.hpp"
#include "services/output/output.hpp"
#include "services/output/route_plan.hpp"

namespace diskos::output {

/** Where one transition goes. */
struct TransitionPlan {
    std::optional<Output> from{};  // nullopt when unknown
    Output to{};
    bool suspend = false;  // Pause playback first, if it plays
};

/**
 * One move of the player's audio to another output: it pauses playback if asked to, then sends
 * the route's steps (plan_route()) to one player instance. It neither snapshots nor restores
 * playback: the router does, as it may retry.
 *
 * It routes only once the player has reported the pause: a 0666 while the player still plays
 * crashes it. Nor does it wait for the audio device to go quiet: on V2.57 a paused player keeps
 * its PCM RUNNING (Cast handoff notes; checked on the internal DAC on 2026-11-07), so that never
 * comes. The player takes its commands in order, so a pause it reported lands before the 0666.
 *
 * It keeps pointers to the sender, the playback and the USB DAC, which must outlive it.
 */
class Transition {
public:
    enum class Status : std::uint8_t {
        Running,
        Done,
        Failed,
    };

    enum class Failure : std::uint8_t {
        SendFailed,     // A command was not queued
        NotPaused,      // The player did not report the pause in time
        NoUsbDac,       // The route is to USB audio, and no USB DAC is plugged in
        WrongInstance,  // The player restarted
    };

    // How long the player has to report the pause; it does within ~15 ms (capture of 2026-11-07).
    static constexpr Clock::duration kPauseTimeout = std::chrono::seconds{2};
    // How long the player has to report its state when it is unknown.
    static constexpr Clock::duration kStateTimeout = std::chrono::seconds{1};

    Transition(
            const TransitionPlan& plan,
            player::PlayerInstance instance,
            player::CommandSender& sender,
            PlaybackControl& playback,
            UsbDac& usb_dac
    );

    /**
     * Advance to `now`: send every step that is due, up to the next wait.
     *  - Suspending: if the play state is unknown, it asks for it and waits up to kStateTimeout,
     *    then goes on as if not playing. If playing, it pauses and waits up to kPauseTimeout for
     *    the player to report Paused or Stopped, else fails with NotPaused.
     *  - Routing: for USB audio, it checks for a USB DAC first and fails with NoUsbDac before any
     *    frame. It tells the playback of the output reset as soon as the first 0666 is sent.
     * Returns Running until it is done or has failed (failure()).
     */
    [[nodiscard]] Status advance(Clock::time_point now);

    // Properties

    [[nodiscard]] const Output& target() const noexcept {
        return target_;
    }

    /** Get why it failed, or nullopt if it has not. */
    [[nodiscard]] std::optional<Failure> failure() const noexcept {
        return failure_;
    }

    /** Check whether it paused playback. */
    [[nodiscard]] bool paused() const noexcept {
        return paused_;
    }

    /** Check whether a 0666 was sent: the output is then no longer what it was. */
    [[nodiscard]] bool output_reset() const noexcept {
        return output_reset_;
    }

private:
    enum class Phase : std::uint8_t {
        Starting,
        AwaitingState,
        AwaitingPause,
        Routing,
        Finished,
    };

    Status start(Clock::time_point now);
    Status await_state(Clock::time_point now);
    Status await_pause(Clock::time_point now);
    /** Pause if `state` is playing, else start routing. */
    Status suspend(playback::PlayState state, Clock::time_point now);
    Status begin_routing(Clock::time_point now);
    Status route(Clock::time_point now);
    Status send(const player::Command& command, Clock::time_point now);
    Status fail(Failure failure);

    Output target_;
    std::vector<Step> steps_;
    std::size_t next_step_ = 0;
    bool suspend_;
    player::PlayerInstance instance_;
    player::CommandSender* sender_;
    PlaybackControl* playback_;
    UsbDac* usb_dac_;
    Phase phase_ = Phase::Starting;
    Clock::time_point wait_until_{};  // The end of the current wait
    std::optional<Failure> failure_{};
    bool paused_ = false;
    bool output_reset_ = false;
};

}  // namespace diskos::output

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "player/command_sender.hpp"
#include "player/player_instance.hpp"
#include "services/output/interfaces.hpp"

namespace diskos::output {

/**
 * When a player instance may be given an output route.
 *
 * After its command queue opens, a starting player still runs its late start-up on a thread of
 * its own (RE of V2.40's mq_player, 0x4dca64; V2.57 needs the same wait, as the legacy local init
 * found): ~50 polls 100 ms apart, then up to ~2 s of 1 s sleeps,
 * resetting the work mode and re-initialising the output as the hardware comes up. A 0666 or 0657
 * sent meanwhile is undone. No frame says it is over, and an idle player sends nothing unasked:
 * so readiness asks for the state until the player answers, then waits kSettle.
 * A player that had settled before (a UI restart) is ready on its first answer.
 *
 * It keeps a pointer to the sender, which must outlive it.
 */
class PlayerReadiness {
public:
    // Past the ~7 s worst case of the player's late start-up.
    static constexpr Clock::duration kSettle = std::chrono::seconds{9};
    // How often to ask for the state until the player answers.
    static constexpr Clock::duration kStateRequestInterval = std::chrono::milliseconds{500};

    explicit PlayerReadiness(player::CommandSender& sender) : sender_(&sender) {}

    /**
    Start over for the new player `instance`.
    `settled`: Whether the player had settled before (e.g. a UI restart)
    */
    void start(player::PlayerInstance instance, Clock::time_point now, bool settled);

    /** Note that the player answered. */
    void note_reply(Clock::time_point now);

    /**
     * Advance to `now`: until the player answers, ask it for its state every
     * kStateRequestInterval. A request that is not sent is simply made again at the next one.
     * Returns true if the player just became ready.
     */
    [[nodiscard]] bool advance(Clock::time_point now);

    /** Check whether the player may be given a route. */
    [[nodiscard]] bool ready() const noexcept {
        return phase_ == Phase::Ready;
    }

private:
    enum class Phase : std::uint8_t {
        NoPlayer,
        AwaitingReply,
        Settling,
        Ready,
    };

    player::CommandSender* sender_;
    std::optional<player::PlayerInstance> instance_{};
    Phase phase_ = Phase::NoPlayer;
    bool settled_ = false;                    // The player had settled before: ready on its answer
    Clock::time_point ready_at_{};            // While Settling
    Clock::time_point next_state_request_{};  // While AwaitingReply
};

}  // namespace diskos::output

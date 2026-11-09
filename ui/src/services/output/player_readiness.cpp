// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/output/player_readiness.hpp"

#include "player/commands.hpp"

namespace diskos::output {

void PlayerReadiness::start(player::PlayerInstance instance, Clock::time_point now, bool settled) {
    instance_ = instance;
    phase_ = Phase::AwaitingReply;
    settled_ = settled;
    next_state_request_ = now;
}

void PlayerReadiness::note_reply(Clock::time_point now) {
    if (phase_ == Phase::AwaitingReply) {
        phase_ = Phase::Settling;
        ready_at_ = settled_ ? now : now + kSettle;
    }
}

bool PlayerReadiness::advance(Clock::time_point now) {
    if (phase_ == Phase::AwaitingReply && now >= next_state_request_) {
        (void)sender_->send(player::RequestStateCommand{}, instance_);
        next_state_request_ = now + kStateRequestInterval;
    }
    if (phase_ != Phase::Settling || now < ready_at_) {
        return false;
    }
    phase_ = Phase::Ready;
    return true;
}

}  // namespace diskos::output

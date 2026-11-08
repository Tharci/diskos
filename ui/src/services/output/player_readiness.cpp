// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/output/player_readiness.hpp"

namespace diskos::output {

void PlayerReadiness::start(Clock::time_point now, bool settled) {
    phase_ = Phase::AwaitingReply;
    settled_ = settled;
    next_probe_ = now;
}

void PlayerReadiness::note_reply(Clock::time_point now) {
    if (phase_ == Phase::AwaitingReply) {
        phase_ = Phase::Settling;
        ready_at_ = settled_ ? now : now + kSettle;
    }
}

bool PlayerReadiness::advance(Clock::time_point now) {
    if (phase_ != Phase::Settling || now < ready_at_) {
        return false;
    }
    phase_ = Phase::Ready;
    return true;
}

bool PlayerReadiness::take_probe(Clock::time_point now) {
    if (phase_ != Phase::AwaitingReply || now < next_probe_) {
        return false;
    }
    next_probe_ = now + kProbeInterval;
    return true;
}

}  // namespace diskos::output

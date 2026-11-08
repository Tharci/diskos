// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/output/transition.hpp"

#include <utility>
#include <variant>

namespace diskos::output {
namespace {

using playback::PlayState;

bool is_playing(PlayState state) {
    return state == PlayState::Playing || state == PlayState::Loading;
}

Transition::Failure failure_of(player::SendError error) {
    return error == player::SendError::WrongInstance ? Transition::Failure::WrongInstance
                                                     : Transition::Failure::SendFailed;
}

}  // namespace

Transition::Transition(
        const TransitionPlan& plan,
        player::PlayerInstance instance,
        player::CommandSender& sender,
        PlaybackControl& playback,
        AudioDevices& devices
)
    : target_(plan.to),
      steps_(plan_route(plan.from, plan.to)),
      suspend_(plan.suspend),
      instance_(instance),
      sender_(&sender),
      playback_(&playback),
      devices_(&devices) {}

Transition::Status Transition::advance(Clock::time_point now) {
    switch (phase_) {
        case Phase::Starting:
            return start(now);
        case Phase::AwaitingState:
            return await_state(now);
        case Phase::AwaitingPause:
            return await_pause(now);
        case Phase::Routing:
            return route(now);
        case Phase::Finished:
            return failure_ ? Status::Failed : Status::Done;
    }
    std::unreachable();
}

Transition::Status Transition::start(Clock::time_point now) {
    if (!suspend_) {
        return begin_routing(now);
    }
    const PlayState state = playback_->play_state();
    if (state != PlayState::Unknown) {
        return suspend(state, now);
    }
    const SendResult asked = sender_->send(player::RequestStateCommand{}, instance_);
    if (!asked) {
        return fail(failure_of(asked.error()));
    }
    phase_ = Phase::AwaitingState;
    wait_until_ = now + kStateTimeout;
    return Status::Running;
}

Transition::Status Transition::await_state(Clock::time_point now) {
    const PlayState state = playback_->play_state();
    if (state != PlayState::Unknown) {
        return suspend(state, now);
    }
    return now < wait_until_ ? Status::Running : begin_routing(now);
}

Transition::Status Transition::await_pause(Clock::time_point now) {
    const PlayState state = playback_->play_state();
    if (state == PlayState::Paused || state == PlayState::Stopped) {
        return begin_routing(now);
    }
    return now < wait_until_ ? Status::Running : fail(Failure::NotPaused);
}

Transition::Status Transition::suspend(PlayState state, Clock::time_point now) {
    if (!is_playing(state)) {
        return begin_routing(now);
    }
    const SendResult sent = playback_->pause(now);
    if (!sent) {
        return fail(failure_of(sent.error()));
    }
    paused_ = true;
    phase_ = Phase::AwaitingPause;
    wait_until_ = now + kPauseTimeout;
    return Status::Running;
}

Transition::Status Transition::begin_routing(Clock::time_point now) {
    if (std::holds_alternative<UsbAudio>(target_) && !devices_->usb_dac_present()) {
        return fail(Failure::NoUsbDac);
    }
    phase_ = Phase::Routing;
    wait_until_ = now;
    return route(now);
}

Transition::Status Transition::route(Clock::time_point now) {
    if (now < wait_until_) {
        return Status::Running;
    }
    while (next_step_ < steps_.size()) {
        const Step& step = steps_[next_step_++];
        if (const auto* wait = std::get_if<WaitStep>(&step)) {
            wait_until_ = now + wait->duration;
            return Status::Running;
        }
        if (const auto* sent = std::get_if<SendStep>(&step)) {
            if (send(sent->command, now) == Status::Failed) {
                return Status::Failed;
            }
        }
    }
    phase_ = Phase::Finished;
    return Status::Done;
}

Transition::Status Transition::send(const player::Command& command, Clock::time_point now) {
    const SendResult sent = sender_->send(command, instance_);
    if (!sent) {
        return fail(failure_of(sent.error()));
    }
    if (!output_reset_ && std::holds_alternative<player::RouteOutputCommand>(command)) {
        output_reset_ = true;
        playback_->note_output_reset(now);
    }
    return Status::Running;
}

Transition::Status Transition::fail(Failure failure) {
    failure_ = failure;
    phase_ = Phase::Finished;
    return Status::Failed;
}

}  // namespace diskos::output

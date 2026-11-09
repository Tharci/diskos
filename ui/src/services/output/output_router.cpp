// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/output/output_router.hpp"

#include <utility>
#include <variant>

namespace diskos::output {
namespace {

using playback::PlayState;

bool is_playing(PlayState state) {
    return state == PlayState::Playing || state == PlayState::Loading;
}

/** The refusal for a blocker the user must clear first, or nullopt for one that passes by itself.
 */
std::optional<Refusal> refusal_of(Blocker blocker) {
    switch (blocker) {
        case Blocker::CardNotReady:
            return Refusal::CardNotReady;
        case Blocker::OtherSource:
            return Refusal::OtherSource;
        case Blocker::Audiobook:
            return Refusal::Audiobook;
        case Blocker::PlayStarting:
        case Blocker::Casting:
            return std::nullopt;
    }
    std::unreachable();
}

/** Whether `report` shows that the player takes commands, as the answer to a state request does. */
bool is_answer(const player::Report& report) {
    return std::holds_alternative<player::TrackReport>(report)
           || std::holds_alternative<player::PlayStateReport>(report)
           || std::holds_alternative<player::PositionReport>(report);
}

/** Whether a Departure goes ahead despite `blocker`: only the source and the card stop it. */
bool departure_passes(Blocker blocker) {
    return blocker != Blocker::OtherSource && blocker != Blocker::CardNotReady;
}

}  // namespace

OutputRouter::OutputRouter(
        player::CommandSender& sender,
        PlaybackControl& playback,
        UsbDac& usb_dac,
        Admission& admission,
        RouteStore& store
)
    : sender_(&sender),
      playback_(&playback),
      usb_dac_(&usb_dac),
      admission_(&admission),
      store_(&store),
      readiness_(sender) {}

std::expected<void, Refusal> OutputRouter::select(const Output& target) {
    if (target == desired_) {
        return {};
    }
    if (const std::optional<Blocker> blocker = admission_->blocker()) {
        if (const std::optional<Refusal> refusal = refusal_of(*blocker)) {
            return std::unexpected{*refusal};
        }
    }
    if (std::holds_alternative<UsbAudio>(target) && !usb_dac_->connected()) {
        return std::unexpected{Refusal::NoUsbDac};
    }
    desired_ = target;
    intent_ = true;
    failures_ = 0;
    return {};
}

void OutputRouter::bluetooth_connected(const BluetoothAddress& device) {
    if (lost_ == device) {
        lost_.reset();
    }
    if (std::holds_alternative<Spdif>(desired_) || std::holds_alternative<UsbAudio>(desired_)) {
        return;
    }
    const Output sink = Bluetooth{.device = device};
    if (desired_ != sink) {
        desired_ = sink;
        intent_ = true;
        failures_ = 0;
    }
}

void OutputRouter::bluetooth_lost(const BluetoothAddress& device) {
    lost_ = device;
    if (desired_ == Output{Bluetooth{.device = device}}) {
        desired_ = InternalDac{};
        intent_ = true;
        failures_ = 0;
    }
}

void OutputRouter::on_connected(player::PlayerInstance instance, Clock::time_point now) {
    instance_ = instance;
    transition_.reset();
    snapshot_.reset();
    lost_.reset();
    intent_ = false;
    recovering_ = false;
    failures_ = 0;
    retry_at_ = {};
    applied_.reset();
    bool settled = false;
    if (!std::exchange(loaded_, true)) {
        if (const std::optional<StoredRoute> stored = store_->load()) {
            applied_ = stored->applied;
            settled = stored->settled;
            desired_ = applied_.value_or(desired_);  // Keep what the player has
        }
    }
    readiness_.start(instance, now, settled);
    save();
}

void OutputRouter::on_report(const player::Report& report, Clock::time_point now) {
    if (is_answer(report)) {
        readiness_.note_reply(now);
    }
}

void OutputRouter::source_changed() {
    transition_.reset();
    snapshot_.reset();
    recovering_ = false;
    applied_.reset();
    save();
}

std::vector<Outcome> OutputRouter::advance(Clock::time_point now) {
    std::vector<Outcome> outcomes;
    if (!instance_) {
        return outcomes;
    }
    if (readiness_.advance(now)) {
        save();
    }
    if (!transition_ && may_start(now)) {
        start(*instance_);
    }
    run(now, outcomes);
    return outcomes;
}

OutputRouter::Kind OutputRouter::next_kind() const {
    if (departing()) {
        return Kind::Departure;
    }
    return intent_ ? Kind::Change : Kind::Correction;
}

bool OutputRouter::departing() const {
    return lost_ && applied_ == Output{Bluetooth{.device = *lost_}};
}

bool OutputRouter::may_start(Clock::time_point now) {
    if (!readiness_.ready() || applied_ == desired_ || now < retry_at_) {
        return false;
    }
    const Kind kind = next_kind();
    const std::optional<Blocker> blocker = admission_->blocker();
    if (blocker && (kind != Kind::Departure || !departure_passes(*blocker))) {
        return false;
    }
    return kind != Kind::Correction || !is_playing(playback_->play_state());
}

void OutputRouter::start(player::PlayerInstance instance) {
    kind_ = next_kind();
    intent_ = false;
    if (!snapshot_) {
        snapshot_ = playback_->snapshot();  // A half-applied move keeps the one from before it
    }
    const TransitionPlan plan{
            .from = applied_,
            .to = desired_,
            .suspend = kind_ == Kind::Change,
    };
    transition_.emplace(plan, instance, *sender_, *playback_, *usb_dac_);
}

void OutputRouter::run(Clock::time_point now, std::vector<Outcome>& outcomes) {
    if (!transition_) {
        return;
    }
    const Transition::Status status = transition_->advance(now);
    if (status == Transition::Status::Running) {
        return;
    }
    const Transition finished = *std::move(transition_);
    transition_.reset();
    if (status == Transition::Status::Done) {
        on_done(finished, now, outcomes);
    } else {
        on_failed(finished, now, outcomes);
    }
}

void OutputRouter::on_done(
        const Transition& done, Clock::time_point now, std::vector<Outcome>& outcomes
) {
    applied_ = done.target();
    recovering_ = false;
    failures_ = 0;
    retry_at_ = {};
    if (!departing()) {
        lost_.reset();
    }
    save();
    outcomes.push_back(Outcome{.kind = Outcome::Kind::Switched, .output = done.target()});
    const playback::Resume when =
            kind_ == Kind::Departure ? playback::Resume::OnNextPlay : playback::Resume::AsBefore;
    restore(when, done.target(), now, outcomes);
}

void OutputRouter::on_failed(
        const Transition& failed, Clock::time_point now, std::vector<Outcome>& outcomes
) {
    using enum Transition::Failure;
    const Transition::Failure failure = failed.failure().value_or(SendFailed);
    const Output& target = failed.target();
    if (failure == WrongInstance) {
        snapshot_.reset();  // on_connected() follows
        return;
    }
    if (failed.output_reset()) {
        applied_.reset();
        save();
        retry_at_ = now + kRecoveryRetry;
        if (!std::exchange(recovering_, true)) {
            outcomes.push_back(Outcome{.kind = Outcome::Kind::Recovering, .output = target});
        }
        return;
    }
    if (failure == NoUsbDac) {
        revert_desired();
        outcomes.push_back(Outcome{.kind = Outcome::Kind::NoUsbDac, .output = target});
    }
    if (recovering_) {
        retry_at_ = now + kRecoveryRetry;  // Still half-applied: the snapshot waits for a retry
        return;
    }
    if (failed.paused()) {
        resume(target, now, outcomes);
    }
    snapshot_.reset();
    retry_at_ = now + kRetryWait;
    if (failure == NoUsbDac || kind_ != Kind::Change) {
        return;  // Corrections and Departures are retried quietly
    }
    intent_ = true;
    if (++failures_ < kMaxAttempts) {
        outcomes.push_back(Outcome{.kind = Outcome::Kind::Retrying, .output = target});
        return;
    }
    revert_desired();
    outcomes.push_back(Outcome{.kind = Outcome::Kind::Failed, .output = target});
}

void OutputRouter::resume(
        const Output& target, Clock::time_point now, std::vector<Outcome>& outcomes
) {
    // If the pause never landed, it still plays: a toggle would pause it.
    if (!is_playing(playback_->play_state())) {
        restore(playback::Resume::AsBefore, target, now, outcomes);
    }
}

void OutputRouter::restore(
        playback::Resume when,
        const Output& target,
        Clock::time_point now,
        std::vector<Outcome>& outcomes
) {
    if (!snapshot_) {
        return;
    }
    const SendResult restored = playback_->restore(*snapshot_, when, now);
    snapshot_.reset();
    if (!restored && restored.error() == player::SendError::Failed) {
        outcomes.push_back(Outcome{.kind = Outcome::Kind::ResumeFailed, .output = target});
    }
}

void OutputRouter::revert_desired() {
    desired_ = applied_.value_or(InternalDac{});
    intent_ = false;
    failures_ = 0;
}

void OutputRouter::save() {
    store_->save({.applied = applied_, .settled = readiness_.ready()});
}

}  // namespace diskos::output

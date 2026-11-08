// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/output/transition.hpp"

#include <chrono>

#include "services/output/output.hpp"
#include "services/output/test_fakes.hpp"
#include "services/playback/player_state.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::output {
namespace {

using namespace std::chrono_literals;
using namespace testing;
using playback::PlayState;
using Status = Transition::Status;

const Clock::time_point kStart = Clock::time_point{} + 100s;

/** A transition over fakes. */
class Fixture {
public:
    explicit Fixture(const TransitionPlan& plan)
        : transition_(plan, kInstance, sender_, playback_, devices_) {}

    Status at(Clock::duration since_start) {
        return transition_.advance(kStart + since_start);
    }

    Log& log() noexcept {
        return log_;
    }

    FakeSender& sender() noexcept {
        return sender_;
    }

    FakePlayback& playback() noexcept {
        return playback_;
    }

    FakeDevices& devices() noexcept {
        return devices_;
    }

    Transition& transition() noexcept {
        return transition_;
    }

private:
    Log log_;
    FakeSender sender_{log_};
    FakePlayback playback_{log_};
    FakeDevices devices_;
    Transition transition_;
};

const TransitionPlan kToSpdif{.from = InternalDac{}, .to = Spdif{}, .suspend = true};

TEST_CASE(
        "Transition sends its route, noting the output reset at the first 0666",
        "[output][transition]"
) {
    Fixture fixture{kToSpdif};
    const Status first = fixture.at(0ms);
    const Log at_first_wait = fixture.log();
    const Status waiting = fixture.at(64ms);
    const Status done = fixture.at(65ms);
    const Log expected{"0666000C0004", "reset", "0642000C0000", "0657000C0008"};

    CHECK(first == Status::Running);
    CHECK(at_first_wait == Log{"0666000C0004", "reset"});
    CHECK(waiting == Status::Running);
    CHECK(done == Status::Done);
    CHECK(fixture.log() == expected);
    CHECK_FALSE(fixture.transition().paused());
    CHECK(fixture.transition().output_reset());
}

TEST_CASE("Transition routes only once the player reports the pause", "[output][transition]") {
    Fixture fixture{kToSpdif};
    fixture.playback().set_play_state(PlayState::Playing);
    const Status pausing = fixture.at(0ms);
    const Status still_playing = fixture.at(10ms);
    const Log before_report = fixture.log();
    fixture.playback().set_play_state(PlayState::Paused);
    const Status routing = fixture.at(20ms);

    CHECK(pausing == Status::Running);
    CHECK(still_playing == Status::Running);
    CHECK(before_report == Log{"pause"});
    CHECK(routing == Status::Running);
    CHECK(fixture.log() == Log{"pause", "0666000C0004", "reset"});
    CHECK(fixture.transition().paused());
}

TEST_CASE(
        "Transition fails without routing when the pause is not reported", "[output][transition]"
) {
    Fixture fixture{kToSpdif};
    fixture.playback().set_play_state(PlayState::Playing);
    (void)fixture.at(0ms);
    const Status waiting = fixture.at(Transition::kPauseTimeout - 1ms);
    const Status failed = fixture.at(Transition::kPauseTimeout);

    CHECK(waiting == Status::Running);
    CHECK(failed == Status::Failed);
    CHECK(fixture.transition().failure() == Transition::Failure::NotPaused);
    CHECK_FALSE(fixture.transition().output_reset());
    CHECK(fixture.log() == Log{"pause"});
}

TEST_CASE("Transition asks for an unknown play state before suspending", "[output][transition]") {
    Fixture answered{kToSpdif};
    answered.playback().set_play_state(PlayState::Unknown);
    (void)answered.at(0ms);
    answered.playback().set_play_state(PlayState::Playing);
    (void)answered.at(10ms);
    Fixture silent{kToSpdif};
    silent.playback().set_play_state(PlayState::Unknown);
    (void)silent.at(0ms);
    const Status waiting = silent.at(Transition::kStateTimeout - 1ms);
    (void)silent.at(Transition::kStateTimeout);

    CHECK(answered.log() == Log{"02020008", "pause"});
    CHECK(waiting == Status::Running);
    CHECK(silent.log() == Log{"02020008", "0666000C0004", "reset"});
}

TEST_CASE("Transition checks for a USB DAC before any frame", "[output][transition]") {
    Fixture fixture{{.from = InternalDac{}, .to = UsbAudio{}, .suspend = true}};
    fixture.devices().set_usb_dac(false);
    const Status failed = fixture.at(0ms);

    CHECK(failed == Status::Failed);
    CHECK(fixture.transition().failure() == Transition::Failure::NoUsbDac);
    CHECK(fixture.log().empty());
}

TEST_CASE(
        "Transition fails on a send that fails, saying whether the output reset",
        "[output][transition]"
) {
    Fixture fixture{kToSpdif};
    fixture.sender().fail_after(1);
    (void)fixture.at(0ms);
    const Status failed = fixture.at(65ms);
    Fixture restarted{kToSpdif};
    restarted.sender().set_connected(kInstance.next());
    const Status gone = restarted.at(0ms);

    CHECK(failed == Status::Failed);
    CHECK(fixture.transition().failure() == Transition::Failure::SendFailed);
    CHECK(fixture.transition().output_reset());
    CHECK(gone == Status::Failed);
    CHECK(restarted.transition().failure() == Transition::Failure::WrongInstance);
    CHECK_FALSE(restarted.transition().output_reset());
}

}  // namespace
}  // namespace diskos::output

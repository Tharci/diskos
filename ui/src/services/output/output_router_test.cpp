// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/output/output_router.hpp"

#include <chrono>
#include <expected>
#include <optional>
#include <vector>

#include "player/modes.hpp"
#include "player/reports.hpp"
#include "services/output/interfaces.hpp"
#include "services/output/output.hpp"
#include "services/output/test_fakes.hpp"
#include "services/playback/player_state.hpp"
#include "utils/bluetooth_address.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::output {
namespace {

using namespace std::chrono_literals;
using namespace testing;
using playback::PlayState;
using Outcomes = std::vector<Outcome>;

const Clock::time_point kStart = Clock::time_point{} + 100s;

// Built before Catch2 starts, so without REQUIRE: value() throws if it is not an address.
const utils::BluetoothAddress kSpeaker = utils::BluetoothAddress::from("30:82:16:AB:38:A0").value();
const Output kOnSpeaker = Bluetooth{.device = kSpeaker};

// The answer to a state request from a player that is idle.
const player::Report kAnswer = player::PlayStateReport{.state = player::ReportedPlayState::Stopped};

Outcome switched(const Output& output) {
    return {.kind = Outcome::Kind::Switched, .output = output};
}

/** A player connected without a stored route: not ready until it answers and settles. */
struct FreshPlayer {};

/** A router over fakes. */
class Fixture {
public:
    /** A router whose settled player was routed to `applied` (a UI restart), ready at kStart. */
    explicit Fixture(std::optional<Output> applied = InternalDac{}) {
        store_.set_stored(StoredRoute{.applied = std::move(applied), .settled = true});
        router_.on_connected(kInstance, kStart);
        router_.on_report(kAnswer, kStart);
        REQUIRE(at(0ms).empty());
        REQUIRE(router_.ready());
        log_.clear();
    }

    explicit Fixture(FreshPlayer /*fresh*/) {
        router_.on_connected(kInstance, kStart);
    }

    /** Advance to `since_start` after kStart. */
    Outcomes at(Clock::duration since_start) {
        return router_.advance(kStart + since_start);
    }

    OutputRouter& router() noexcept {
        return router_;
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

    FakeUsbDac& usb_dac() noexcept {
        return usb_dac_;
    }

    FakeAdmission& admission() noexcept {
        return admission_;
    }

    FakeStore& store() noexcept {
        return store_;
    }

private:
    Log log_;
    FakeSender sender_{log_};
    FakePlayback playback_{log_};
    FakeUsbDac usb_dac_;
    FakeAdmission admission_;
    FakeStore store_;
    OutputRouter router_{sender_, playback_, usb_dac_, admission_, store_};
};

TEST_CASE(
        "OutputRouter moves to a chosen output, pausing and resuming playback", "[output][router]"
) {
    Fixture fixture;
    fixture.playback().set_play_state(PlayState::Playing);
    const bool chosen = fixture.router().select(Spdif{}).has_value();
    const Outcomes pausing = fixture.at(0ms);
    fixture.playback().set_play_state(PlayState::Paused);
    const Outcomes routing = fixture.at(10ms);
    const bool busy = fixture.router().busy();
    const Outcomes done = fixture.at(100ms);
    const Log expected{
            "pause",
            "0666000C0004",
            "reset",
            "0642000C0000",
            "0657000C0008",
            "restore as before",
    };

    CHECK(chosen);
    CHECK(pausing.empty());
    CHECK(routing.empty());
    CHECK(busy);
    CHECK(done == Outcomes{switched(Spdif{})});
    CHECK(fixture.log() == expected);
    CHECK(fixture.router().applied() == Output{Spdif{}});
    CHECK_FALSE(fixture.router().busy());
}

TEST_CASE("OutputRouter refuses a choice the user must act on first", "[output][router]") {
    Fixture audiobook;
    audiobook.admission().set_blocker(Blocker::Audiobook);
    Fixture other_source;
    other_source.admission().set_blocker(Blocker::OtherSource);
    Fixture no_card;
    no_card.admission().set_blocker(Blocker::CardNotReady);
    Fixture no_dac;
    no_dac.usb_dac().set_connected(false);
    Fixture starting;
    starting.admission().set_blocker(Blocker::PlayStarting);

    CHECK(audiobook.router().select(Spdif{}) == std::unexpected{Refusal::Audiobook});
    CHECK(other_source.router().select(Spdif{}) == std::unexpected{Refusal::OtherSource});
    CHECK(no_card.router().select(Spdif{}) == std::unexpected{Refusal::CardNotReady});
    CHECK(no_dac.router().select(UsbAudio{}) == std::unexpected{Refusal::NoUsbDac});
    CHECK(starting.router().select(Spdif{}).has_value());
    CHECK(audiobook.router().desired() == Output{InternalDac{}});
    CHECK(no_dac.router().desired() == Output{InternalDac{}});
}

TEST_CASE("OutputRouter keeps a choice made while the player settles", "[output][router]") {
    Fixture fixture{FreshPlayer{}};
    const bool chosen = fixture.router().select(Spdif{}).has_value();
    (void)fixture.at(0ms);
    (void)fixture.at(500ms);
    fixture.router().on_report(kAnswer, kStart + 600ms);
    (void)fixture.at(1s);
    const Log while_settling = fixture.log();
    (void)fixture.at(600ms + PlayerReadiness::kSettle);

    CHECK(chosen);
    CHECK(while_settling == Log{"02020008", "02020008"});
    CHECK(fixture.log() == Log{"02020008", "02020008", "0666000C0004", "reset"});
}

TEST_CASE("OutputRouter keeps the stored route after a UI restart", "[output][router]") {
    Fixture fixture{kOnSpeaker};
    const Outcomes after = fixture.at(1s);

    CHECK(fixture.router().desired() == kOnSpeaker);
    CHECK(after.empty());
    CHECK(fixture.log().empty());
}

TEST_CASE("OutputRouter takes only playback reports as the player's answer", "[output][router]") {
    Fixture fixture{FreshPlayer{}};
    fixture.router().on_report(player::ChargingReport{.charging = true}, kStart);
    fixture.router().on_report(player::UnknownReport{.tag = "aa1c", .body = ""}, kStart);
    (void)fixture.at(PlayerReadiness::kSettle);
    const bool after_others = fixture.router().ready();
    fixture.router().on_report(player::PositionReport{.position_ms = 1000}, kStart + 10s);
    (void)fixture.at(10s + PlayerReadiness::kSettle);

    CHECK_FALSE(after_others);
    CHECK(fixture.router().ready());
}

TEST_CASE("OutputRouter lets a connected sink replace the internal DAC only", "[output][router]") {
    Fixture internal;
    internal.router().bluetooth_connected(kSpeaker);
    Fixture spdif;
    REQUIRE(spdif.router().select(Spdif{}).has_value());
    spdif.router().bluetooth_connected(kSpeaker);

    CHECK(internal.router().desired() == kOnSpeaker);
    CHECK(spdif.router().desired() == Output{Spdif{}});
}

TEST_CASE(
        "OutputRouter leaves a lost sink at once, resuming on the next play", "[output][router]"
) {
    Fixture fixture{kOnSpeaker};
    fixture.playback().set_play_state(PlayState::Playing);  // Into the dead sink
    fixture.admission().set_blocker(Blocker::Audiobook);
    fixture.router().bluetooth_lost(kSpeaker);
    const Outcomes leaving = fixture.at(0ms);
    const Log at_once = fixture.log();
    const Outcomes left = fixture.at(100ms);
    const Log expected{
            "0666000C0006",
            "reset",
            "0642000C0000",
            "0657000C0008",
            "restore on next play",
    };

    CHECK(leaving.empty());
    CHECK(at_once == Log{"0666000C0006", "reset"});
    CHECK(left == Outcomes{switched(InternalDac{})});
    CHECK(fixture.log() == expected);
    CHECK(fixture.router().desired() == Output{InternalDac{}});
}

TEST_CASE("OutputRouter leaves a sink the player no longer lists", "[output][router]") {
    Fixture fixture{kOnSpeaker};
    fixture.playback().set_play_state(PlayState::Playing);
    fixture.router().on_report(player::BluetoothSinksReport{.sinks = {kSpeaker}}, kStart);
    const Outcomes listed = fixture.at(0ms);
    const Log while_listed = fixture.log();
    fixture.router().on_report(player::BluetoothSinksReport{}, kStart + 1s);
    (void)fixture.at(1s);

    CHECK(listed.empty());
    CHECK(while_listed.empty());
    CHECK(fixture.log() == Log{"0666000C0006", "reset"});
    CHECK(fixture.router().desired() == Output{InternalDac{}});
}

TEST_CASE("OutputRouter ignores the player's sinks while on another output", "[output][router]") {
    // The list is empty until the move to the speaker starts the player's Bluetooth.
    Fixture fixture;
    fixture.router().bluetooth_connected(kSpeaker);
    fixture.router().on_report(player::BluetoothSinksReport{}, kStart);
    (void)fixture.at(0ms);

    CHECK(fixture.router().desired() == kOnSpeaker);
    CHECK(fixture.log().front() == "0666000C0006");
    CHECK(fixture.log().size() > 2);
}

TEST_CASE("OutputRouter makes a move off a lost sink a Departure", "[output][router]") {
    // S/PDIF was chosen while on the speaker, then the speaker went: no pause on the way.
    Fixture fixture{kOnSpeaker};
    fixture.playback().set_play_state(PlayState::Playing);
    REQUIRE(fixture.router().select(Spdif{}).has_value());
    fixture.router().bluetooth_lost(kSpeaker);
    (void)fixture.at(0ms);

    CHECK(fixture.log() == Log{"0666000C0006", "reset"});
    CHECK(fixture.router().desired() == Output{Spdif{}});
}

TEST_CASE("OutputRouter holds a Change while a play starts or Cast plays", "[output][router]") {
    Fixture fixture;
    fixture.admission().set_blocker(Blocker::PlayStarting);
    REQUIRE(fixture.router().select(Spdif{}).has_value());
    (void)fixture.at(0ms);
    fixture.admission().set_blocker(Blocker::Casting);
    (void)fixture.at(10ms);
    const Log while_blocked = fixture.log();
    fixture.admission().set_blocker(std::nullopt);
    (void)fixture.at(20ms);

    CHECK(while_blocked.empty());
    CHECK(fixture.log() == Log{"0666000C0004", "reset"});
}

TEST_CASE("OutputRouter holds a Correction until playback stops", "[output][router]") {
    Fixture fixture;
    fixture.router().source_changed();
    fixture.playback().set_play_state(PlayState::Playing);
    (void)fixture.at(0ms);
    const Log while_playing = fixture.log();
    fixture.playback().set_play_state(PlayState::Paused);
    (void)fixture.at(10ms);
    const Outcomes done = fixture.at(100ms);
    const Log expected{
            "0666000C0006",
            "reset",
            "0642000C0000",
            "0657000C0008",
            "restore as before",
    };

    CHECK(while_playing.empty());
    CHECK(done == Outcomes{switched(InternalDac{})});
    CHECK(fixture.log() == expected);
}

TEST_CASE(
        "OutputRouter retries a Change that failed with nothing changed, then gives up",
        "[output][router]"
) {
    using enum Outcome::Kind;
    Fixture fixture;
    fixture.sender().fail_after(0);
    REQUIRE(fixture.router().select(Spdif{}).has_value());
    const Outcomes first = fixture.at(0ms);
    const Outcomes waiting = fixture.at(OutputRouter::kRetryWait - 1ms);
    const Outcomes second = fixture.at(OutputRouter::kRetryWait);
    const Outcomes third = fixture.at(2 * OutputRouter::kRetryWait);
    const Outcomes after = fixture.at(3 * OutputRouter::kRetryWait);

    CHECK(first == Outcomes{{.kind = Retrying, .output = Spdif{}}});
    CHECK(waiting.empty());
    CHECK(second == Outcomes{{.kind = Retrying, .output = Spdif{}}});
    CHECK(third == Outcomes{{.kind = Failed, .output = Spdif{}}});
    CHECK(after.empty());
    CHECK(fixture.router().desired() == Output{InternalDac{}});
    CHECK(fixture.log().empty());
}

TEST_CASE("OutputRouter retries a move that failed halfway until one lands", "[output][router]") {
    Fixture fixture;
    fixture.playback().set_play_state(PlayState::Playing);
    REQUIRE(fixture.router().select(Spdif{}).has_value());
    (void)fixture.at(0ms);
    fixture.playback().set_play_state(PlayState::Paused);
    fixture.sender().fail_after(1);
    (void)fixture.at(10ms);
    const Outcomes failed = fixture.at(100ms);
    const bool busy = fixture.router().busy();
    const std::optional<Output> unknown = fixture.router().applied();
    fixture.sender().fail_after(std::nullopt);
    const Outcomes waiting = fixture.at(100ms + OutputRouter::kRecoveryRetry - 1ms);
    (void)fixture.at(100ms + OutputRouter::kRecoveryRetry);
    const Outcomes landed = fixture.at(1s);
    const Log expected{
            "pause",
            "0666000C0004",
            "reset",
            "0666000C0004",
            "reset",
            "0642000C0000",
            "0657000C0008",
            "restore as before",
    };

    CHECK(failed == Outcomes{{.kind = Outcome::Kind::Recovering, .output = Spdif{}}});
    CHECK(busy);
    CHECK_FALSE(unknown.has_value());
    CHECK(waiting.empty());
    CHECK(landed == Outcomes{switched(Spdif{})});
    CHECK(fixture.log() == expected);
    CHECK_FALSE(fixture.router().busy());
}

TEST_CASE("OutputRouter does not resume a track whose pause never landed", "[output][router]") {
    Fixture fixture;
    fixture.playback().set_play_state(PlayState::Playing);
    REQUIRE(fixture.router().select(Spdif{}).has_value());
    (void)fixture.at(0ms);
    const Outcomes failed = fixture.at(Transition::kPauseTimeout);

    CHECK(failed == Outcomes{{.kind = Outcome::Kind::Retrying, .output = Spdif{}}});
    CHECK(fixture.log() == Log{"pause"});
}

TEST_CASE("OutputRouter resumes and goes back when no USB DAC is left", "[output][router]") {
    Fixture fixture;
    fixture.playback().set_play_state(PlayState::Playing);
    REQUIRE(fixture.router().select(UsbAudio{}).has_value());
    (void)fixture.at(0ms);
    fixture.usb_dac().set_connected(false);
    fixture.playback().set_play_state(PlayState::Paused);
    const Outcomes failed = fixture.at(10ms);

    CHECK(failed == Outcomes{{.kind = Outcome::Kind::NoUsbDac, .output = UsbAudio{}}});
    CHECK(fixture.log() == Log{"pause", "restore as before"});
    CHECK(fixture.router().desired() == Output{InternalDac{}});
}

TEST_CASE("OutputRouter drops a move when the player restarts", "[output][router]") {
    Fixture fixture;
    fixture.playback().set_play_state(PlayState::Playing);
    REQUIRE(fixture.router().select(Spdif{}).has_value());
    (void)fixture.at(0ms);
    fixture.router().on_connected(kInstance.next(), kStart + 10ms);
    fixture.playback().set_play_state(PlayState::Paused);
    (void)fixture.at(20ms);

    CHECK_FALSE(fixture.router().busy());
    CHECK_FALSE(fixture.router().ready());
    CHECK_FALSE(fixture.router().applied().has_value());
    CHECK(fixture.router().desired() == Output{Spdif{}});
    CHECK(fixture.log() == Log{"pause"});
}

TEST_CASE("OutputRouter runs a choice made during a move right after it", "[output][router]") {
    Fixture fixture;
    REQUIRE(fixture.router().select(Spdif{}).has_value());
    (void)fixture.at(0ms);
    REQUIRE(fixture.router().select(UsbAudio{}).has_value());
    const Outcomes first = fixture.at(100ms);
    const Outcomes second = fixture.at(200ms);
    const Log expected{
            "0666000C0004",
            "reset",
            "0642000C0000",
            "0657000C0008",
            "restore as before",
            "0666000C0003",
            "reset",
            "0642000C0005",
            "0657000C0008",
            "restore as before",
    };

    CHECK(first == Outcomes{switched(Spdif{})});
    CHECK(second == Outcomes{switched(UsbAudio{})});
    CHECK(fixture.log() == expected);
}

TEST_CASE(
        "OutputRouter reads the store on the first connection only, and keeps it current",
        "[output][router]"
) {
    Fixture fixture{FreshPlayer{}};
    const std::optional<StoredRoute> connected = fixture.store().saved();
    fixture.router().on_report(kAnswer, kStart);
    (void)fixture.at(PlayerReadiness::kSettle);
    const std::optional<StoredRoute> ready = fixture.store().saved();
    (void)fixture.at(PlayerReadiness::kSettle + 100ms);  // The Correction to the internal DAC lands
    const std::optional<StoredRoute> routed = fixture.store().saved();
    fixture.store().set_stored(StoredRoute{.applied = Spdif{}, .settled = true});
    fixture.router().on_connected(kInstance.next(), kStart + 20s);

    REQUIRE(connected.has_value());
    CHECK_FALSE(connected->applied.has_value());
    CHECK_FALSE(connected->settled);
    REQUIRE(ready.has_value());
    CHECK(ready->settled);
    REQUIRE(routed.has_value());
    CHECK(routed->applied == Output{InternalDac{}});
    CHECK_FALSE(fixture.router().applied().has_value());
    CHECK_FALSE(fixture.router().ready());
}

TEST_CASE("OutputRouter prepares a play from idle on the applied output", "[output][router]") {
    Fixture spdif{Output{Spdif{}}};
    const bool prepared = spdif.router().prepare_play().has_value();
    Fixture speaker{kOnSpeaker};
    const bool nothing_to_prepare = speaker.router().prepare_play().has_value();

    CHECK(prepared);
    CHECK(spdif.log() == Log{"0666000C0004", "0657000C0008"});
    CHECK(spdif.router().applied() == Output{Spdif{}});
    CHECK(nothing_to_prepare);
    CHECK(speaker.log().empty());
}

TEST_CASE("OutputRouter prepares nothing while a move runs", "[output][router]") {
    Fixture fixture;
    fixture.playback().set_play_state(PlayState::Playing);
    REQUIRE(fixture.router().select(Spdif{}).has_value());
    (void)fixture.at(0ms);
    const bool prepared = fixture.router().prepare_play().has_value();

    CHECK(prepared);
    CHECK(fixture.log() == Log{"pause"});
}

TEST_CASE("OutputRouter takes a prepared route once the player plays on it", "[output][router]") {
    Fixture fixture{FreshPlayer{}};
    const bool prepared = fixture.router().prepare_play().has_value();
    const std::optional<Output> before_playing = fixture.router().applied();
    fixture.playback().set_play_state(PlayState::Playing);
    (void)fixture.at(0ms);
    const std::optional<Output> playing = fixture.router().applied();
    fixture.router().on_report(kAnswer, kStart);
    (void)fixture.at(PlayerReadiness::kSettle);

    CHECK(prepared);
    CHECK_FALSE(before_playing.has_value());
    CHECK(playing == Output{InternalDac{}});
    // The preamble, then the state request: no correction once the player settles.
    CHECK(fixture.log() == Log{"0666000C0006", "0657000C0008", "02020008"});
}

TEST_CASE("OutputRouter still moves a prepared play to a wanted speaker", "[output][router]") {
    Fixture fixture{FreshPlayer{}};
    fixture.router().bluetooth_connected(kSpeaker);
    REQUIRE(fixture.router().prepare_play().has_value());  // To the internal DAC meanwhile
    fixture.playback().set_play_state(PlayState::Playing);
    (void)fixture.at(0ms);
    fixture.router().on_report(kAnswer, kStart);
    (void)fixture.at(PlayerReadiness::kSettle);

    CHECK(fixture.log() == Log{"0666000C0006", "0657000C0008", "02020008", "pause"});
}

}  // namespace
}  // namespace diskos::output

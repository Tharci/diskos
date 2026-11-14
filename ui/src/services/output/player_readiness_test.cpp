// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/output/player_readiness.hpp"

#include <chrono>

#include "services/output/test_fakes.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::output {
namespace {

using namespace std::chrono_literals;
using namespace testing;

const Clock::time_point kStart = Clock::time_point{} + 100s;

/** A readiness over a fake sender. */
class Fixture {
public:
    Log& log() noexcept {
        return log_;
    }

    FakeSender& sender() noexcept {
        return sender_;
    }

    PlayerReadiness& readiness() noexcept {
        return readiness_;
    }

private:
    Log log_;
    FakeSender sender_{log_};
    PlayerReadiness readiness_{sender_};
};

TEST_CASE("PlayerReadiness asks for the state until the player answers", "[output][readiness]") {
    Fixture fixture;
    PlayerReadiness& readiness = fixture.readiness();
    (void)readiness.advance(kStart);  // No player yet
    readiness.start(kInstance, kStart, false);
    (void)readiness.advance(kStart);
    (void)readiness.advance(kStart + 499ms);
    (void)readiness.advance(kStart + 500ms);
    readiness.note_reply(kStart + 600ms);
    (void)readiness.advance(kStart + 1100ms);

    CHECK(fixture.log() == Log{"02020008", "02020008"});
}

TEST_CASE(
        "PlayerReadiness asks again at the next interval after a failed send", "[output][readiness]"
) {
    Fixture fixture;
    PlayerReadiness& readiness = fixture.readiness();
    fixture.sender().fail_after(0);
    readiness.start(kInstance, kStart, false);
    (void)readiness.advance(kStart);
    fixture.sender().fail_after(std::nullopt);
    (void)readiness.advance(kStart + 499ms);
    (void)readiness.advance(kStart + 500ms);

    CHECK(fixture.log() == Log{"02020008"});
}

TEST_CASE("PlayerReadiness is ready kSettle after the first answer", "[output][readiness]") {
    Fixture fixture;
    PlayerReadiness& readiness = fixture.readiness();
    readiness.start(kInstance, kStart, false);
    const bool unanswered = readiness.advance(kStart + 20s);
    readiness.note_reply(kStart + 20s);
    readiness.note_reply(kStart + 25s);  // Only the first answer counts
    const bool settling = readiness.advance(kStart + 29s - 1ms);
    const bool became_ready = readiness.advance(kStart + 29s);
    const bool again = readiness.advance(kStart + 30s);

    CHECK_FALSE(unanswered);
    CHECK_FALSE(settling);
    CHECK(became_ready);
    CHECK_FALSE(again);
    CHECK(readiness.ready());
}

TEST_CASE(
        "PlayerReadiness is ready on the first answer of a settled player", "[output][readiness]"
) {
    Fixture fixture;
    PlayerReadiness& readiness = fixture.readiness();
    readiness.start(kInstance, kStart, true);
    const bool unanswered = readiness.advance(kStart + 1s);
    readiness.note_reply(kStart + 2s);
    const bool became_ready = readiness.advance(kStart + 2s);

    CHECK_FALSE(unanswered);
    CHECK(became_ready);
}

TEST_CASE("PlayerReadiness starts over for a new player, and asks only it", "[output][readiness]") {
    Fixture fixture;
    PlayerReadiness& readiness = fixture.readiness();
    readiness.start(kInstance, kStart, true);
    readiness.note_reply(kStart);
    (void)readiness.advance(kStart);
    fixture.log().clear();
    readiness.start(kInstance.next(), kStart + 1s, false);
    (void)readiness.advance(kStart + 1s);  // The old instance is still connected: refused
    fixture.sender().set_connected(kInstance.next());
    (void)readiness.advance(kStart + 1500ms);

    CHECK_FALSE(readiness.ready());
    CHECK(fixture.log() == Log{"02020008"});
}

}  // namespace
}  // namespace diskos::output

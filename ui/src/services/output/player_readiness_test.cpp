// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/output/player_readiness.hpp"

#include <chrono>

#include <catch2/catch_test_macros.hpp>

namespace diskos::output {
namespace {

using namespace std::chrono_literals;

const Clock::time_point kStart = Clock::time_point{} + 100s;

TEST_CASE("PlayerReadiness asks for the state until the player answers", "[output][readiness]") {
    PlayerReadiness readiness;
    const bool before_start = readiness.take_probe(kStart);
    readiness.start(kStart, false);
    const bool first = readiness.take_probe(kStart);
    const bool too_soon = readiness.take_probe(kStart + 499ms);
    const bool second = readiness.take_probe(kStart + 500ms);
    readiness.note_reply(kStart + 600ms);
    const bool after_reply = readiness.take_probe(kStart + 1100ms);

    CHECK_FALSE(before_start);
    CHECK(first);
    CHECK_FALSE(too_soon);
    CHECK(second);
    CHECK_FALSE(after_reply);
}

TEST_CASE("PlayerReadiness is ready kSettle after the first answer", "[output][readiness]") {
    PlayerReadiness readiness;
    readiness.start(kStart, false);
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
    PlayerReadiness readiness;
    readiness.start(kStart, true);
    const bool unanswered = readiness.advance(kStart + 1s);
    readiness.note_reply(kStart + 2s);
    const bool became_ready = readiness.advance(kStart + 2s);

    CHECK_FALSE(unanswered);
    CHECK(became_ready);
}

TEST_CASE("PlayerReadiness starts over for a new player", "[output][readiness]") {
    PlayerReadiness readiness;
    readiness.start(kStart, true);
    readiness.note_reply(kStart);
    (void)readiness.advance(kStart);
    readiness.start(kStart + 1s, false);

    CHECK_FALSE(readiness.ready());
    CHECK(readiness.take_probe(kStart + 1s));
}

}  // namespace
}  // namespace diskos::output

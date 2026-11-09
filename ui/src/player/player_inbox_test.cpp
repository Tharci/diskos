// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/player_inbox.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "player/frame.hpp"
#include "player/player_instance.hpp"
#include "player/report_listener.hpp"
#include "player/reports.hpp"
#include "utils/numbers.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::player {
namespace {

using namespace std::chrono_literals;
using Events = std::vector<std::string>;

const ReportListener::Clock::time_point kNow = ReportListener::Clock::time_point{} + 100s;

/** The a103 frame of a position. */
std::string position_frame(std::uint32_t position_ms) {
    return build_frame("a103", utils::format_hex(position_ms, 8));
}

/** Records what it hears, one line per call: "connected 1", "position 6000", "a824". */
class RecordingListener final : public ReportListener {
public:
    void on_connected(PlayerInstance instance, Clock::time_point now) override {
        events_.push_back("connected " + std::to_string(instance.number()));
        times_.push_back(now);
    }

    void on_report(const Report& report, Clock::time_point now) override {
        if (const auto* position = std::get_if<PositionReport>(&report)) {
            events_.push_back("position " + std::to_string(position->position_ms));
        } else if (const auto* unknown = std::get_if<UnknownReport>(&report)) {
            events_.emplace_back(unknown->tag);  // A view into the inbox's copy of the frame
        } else {
            events_.emplace_back("other");
        }
        times_.push_back(now);
    }

    [[nodiscard]] const Events& events() const noexcept {
        return events_;
    }

    [[nodiscard]] const std::vector<Clock::time_point>& times() const noexcept {
        return times_;
    }

private:
    Events events_{};
    std::vector<Clock::time_point> times_{};
};

TEST_CASE("PlayerInbox hands on connections and reports in order", "[player][inbox]") {
    PlayerInbox inbox;
    RecordingListener listener;
    inbox.add_connection(PlayerInstance{1});
    inbox.add_frame(position_frame(6000));
    inbox.add_frame("not a frame");
    inbox.add_frame("a824000C0000");
    inbox.add_connection(PlayerInstance{2});
    inbox.add_frame(position_frame(1000));
    const std::size_t dropped = inbox.deliver(listener, kNow);
    const Events expected{"connected 1", "position 6000", "a824", "connected 2", "position 1000"};

    CHECK(dropped == 0);
    CHECK(listener.events() == expected);
    CHECK(listener.times() == std::vector(5, kNow));
}

TEST_CASE("PlayerInbox delivers each event once", "[player][inbox]") {
    PlayerInbox inbox;
    RecordingListener listener;
    inbox.add_frame(position_frame(1000));
    (void)inbox.deliver(listener, kNow);
    (void)inbox.deliver(listener, kNow + 1s);
    inbox.add_frame(position_frame(2000));
    (void)inbox.deliver(listener, kNow + 2s);

    CHECK(listener.events() == Events{"position 1000", "position 2000"});
}

TEST_CASE("PlayerInbox drops the oldest frames when full, never a connection", "[player][inbox]") {
    PlayerInbox inbox{2};
    RecordingListener listener;
    inbox.add_frame(position_frame(1000));
    inbox.add_connection(PlayerInstance{2});
    inbox.add_frame(position_frame(2000));
    inbox.add_frame(position_frame(3000));
    inbox.add_frame(position_frame(4000));
    const std::size_t dropped = inbox.deliver(listener, kNow);
    inbox.add_frame(position_frame(5000));
    const std::size_t dropped_after = inbox.deliver(listener, kNow);
    const Events expected{"connected 2", "position 3000", "position 4000", "position 5000"};

    CHECK(dropped == 2);
    CHECK(dropped_after == 0);
    CHECK(listener.events() == expected);
}

TEST_CASE("PlayerInbox carries frames from one thread to another", "[player][inbox]") {
    constexpr std::uint32_t kFrames = 2000;
    PlayerInbox inbox{kFrames};
    RecordingListener listener;
    std::jthread receiver{[&inbox] {
        inbox.add_connection(PlayerInstance{1});
        for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
            inbox.add_frame(position_frame(frame));
        }
    }};
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (listener.events().size() < kFrames + 1 && std::chrono::steady_clock::now() < deadline) {
        (void)inbox.deliver(listener, kNow);
        std::this_thread::yield();
    }
    receiver.join();

    REQUIRE(listener.events().size() == kFrames + 1);
    CHECK(listener.events().front() == "connected 1");
    CHECK(listener.events()[1] == "position 0");
    CHECK(listener.events().back() == "position " + std::to_string(kFrames - 1));
}

}  // namespace
}  // namespace diskos::player

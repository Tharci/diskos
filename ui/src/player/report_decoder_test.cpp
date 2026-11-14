// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/report_decoder.hpp"

#include <chrono>
#include <string>
#include <variant>
#include <vector>

#include "player/player_instance.hpp"
#include "player/report_listener.hpp"
#include "player/reports.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::player {
namespace {

using namespace std::chrono_literals;
using Events = std::vector<std::string>;

const ReportListener::Clock::time_point kNow = ReportListener::Clock::time_point{} + 100s;

/** Records what it hears, one line per call: "connected 1", "position 6000", "a824". */
class RecordingListener final : public ReportListener {
public:
    void on_connected(PlayerInstance instance, Clock::time_point /*now*/) override {
        events_.push_back("connected " + std::to_string(instance.number()));
    }

    void on_report(const Report& report, Clock::time_point /*now*/) override {
        if (const auto* position = std::get_if<PositionReport>(&report)) {
            events_.push_back("position " + std::to_string(position->position_ms));
        } else if (const auto* unknown = std::get_if<UnknownReport>(&report)) {
            events_.emplace_back(unknown->tag);  // A view into the frame, valid during the call
        } else {
            events_.emplace_back("other");
        }
    }

    [[nodiscard]] const Events& events() const noexcept {
        return events_;
    }

private:
    Events events_{};
};

TEST_CASE("ReportDecoder hands on connections and reports, nothing else", "[player][decoder]") {
    RecordingListener listener;
    ReportDecoder decoder{listener};
    decoder.on_connected(PlayerInstance{1}, kNow);
    decoder.on_frame("a103000F00001770", kNow);  // A length that does not match: malformed
    decoder.on_frame("a10300100000177000", kNow);
    decoder.on_frame("a103001000001770", kNow);
    decoder.on_frame("a824000C0000", kNow);
    decoder.on_drained(kNow);
    const Events expected{"connected 1", "position 6000", "a824"};

    CHECK(listener.events() == expected);
}

}  // namespace
}  // namespace diskos::player

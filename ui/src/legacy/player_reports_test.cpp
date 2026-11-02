// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/player_reports.h"

#include <array>
#include <cstdint>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace diskos::legacy {
namespace {

diskos_report decode(std::string_view frame) {
    return diskos_decode_report(frame.data(), frame.size());
}

TEST_CASE("diskos_decode_report reduces each report to its kind and value", "[legacy]") {
    // Frames received from the player on the Disc (capture of 2026-11-01).
    struct Expected {
        std::string_view frame;
        diskos_report_kind kind;
        std::uint32_t value;
    };

    const std::array cases{
            Expected{"a103001000C9174D", DISKOS_REPORT_POSITION, 0x00C9174D},
            Expected{"a102000C0004", DISKOS_REPORT_PLAY_MODE, 4},
            Expected{"a714000C003F", DISKOS_REPORT_VOLUME, 63},
            Expected{"a607000C000A", DISKOS_REPORT_SOURCE, 10},
            Expected{"aa1f000C0001", DISKOS_REPORT_CHARGING, 1},
            Expected{"aa23000C0000", DISKOS_REPORT_SCREEN, 0},
            Expected{"aa030008", DISKOS_REPORT_POWER_OFF, 0},
            Expected{"aa05000C0001", DISKOS_REPORT_COVER_ART, 1},
            Expected{"aa01000C0001", DISKOS_REPORT_FAVOURITE, 1},
            Expected{"aa1d000C0057", DISKOS_REPORT_BATTERY, 87},
            Expected{"a810000C0000", DISKOS_REPORT_CONVERSION, 0},
    };
    for (const Expected& expected : cases) {
        const diskos_report report = decode(expected.frame);
        INFO(expected.frame);
        CHECK(report.kind == expected.kind);
        CHECK(report.value == expected.value);
    }
}

TEST_CASE("diskos_decode_report leaves a202 and unknown reports to the caller", "[legacy]") {
    CHECK(decode(R"(a2020013{"state":1})").kind == DISKOS_REPORT_UNDECODED);
    CHECK(decode("aa1c000C0000").kind == DISKOS_REPORT_UNDECODED);
}

TEST_CASE("diskos_decode_report marks refused frames malformed", "[legacy]") {
    CHECK(decode("a103000F000003E8").kind == DISKOS_REPORT_MALFORMED);
    CHECK(decode("aa1f000C0002").kind == DISKOS_REPORT_MALFORMED);
    CHECK(decode(R"(a2020013{"state":7})").kind == DISKOS_REPORT_MALFORMED);
    CHECK(diskos_decode_report(nullptr, 0).kind == DISKOS_REPORT_MALFORMED);
}

}  // namespace
}  // namespace diskos::legacy

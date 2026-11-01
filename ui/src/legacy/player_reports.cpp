// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/player_reports.h"

#include <string_view>
#include <variant>

#include "player/reports.hpp"

namespace diskos::legacy {
namespace {

using namespace player;

diskos_report to_c(const PositionReport& report) {
    return {.kind = DISKOS_REPORT_POSITION, .value = report.position_ms};
}

diskos_report to_c(const PlayModeReport& report) {
    return {.kind = DISKOS_REPORT_PLAY_MODE, .value = static_cast<std::uint32_t>(report.mode)};
}

diskos_report to_c(const VolumeReport& report) {
    return {.kind = DISKOS_REPORT_VOLUME, .value = report.level};
}

diskos_report to_c(const SourceReport& report) {
    return {.kind = DISKOS_REPORT_SOURCE, .value = static_cast<std::uint32_t>(report.source)};
}

diskos_report to_c(const ChargingReport& report) {
    return {.kind = DISKOS_REPORT_CHARGING, .value = report.charging ? 1U : 0U};
}

diskos_report to_c(const ScreenReport& report) {
    return {.kind = DISKOS_REPORT_SCREEN, .value = report.on ? 1U : 0U};
}

diskos_report to_c(const PowerOffReport& /*report*/) {
    return {.kind = DISKOS_REPORT_POWER_OFF, .value = 0};
}

diskos_report to_c(const CoverArtReport& report) {
    return {.kind = DISKOS_REPORT_COVER_ART, .value = report.ready ? 1U : 0U};
}

diskos_report to_c(const FavouriteReport& report) {
    return {.kind = DISKOS_REPORT_FAVOURITE, .value = report.value};
}

diskos_report to_c(const BatteryReport& report) {
    return {.kind = DISKOS_REPORT_BATTERY, .value = report.percent};
}

diskos_report to_c(const ConversionReport& report) {
    return {.kind = DISKOS_REPORT_CONVERSION, .value = report.code};
}

/** The reports the legacy code still reads itself: a202 and anything without a decoder. */
template <typename OtherReport>
diskos_report to_c(const OtherReport& /*report*/) {
    return {.kind = DISKOS_REPORT_UNDECODED, .value = 0};
}

}  // namespace
}  // namespace diskos::legacy

extern "C" diskos_report diskos_decode_report(const char* bytes, size_t size) {
    const auto report = diskos::player::decode_report(std::string_view{bytes, size});
    if (!report) {
        return {.kind = DISKOS_REPORT_MALFORMED, .value = 0};
    }
    return std::visit([](const auto& decoded) { return diskos::legacy::to_c(decoded); }, *report);
}

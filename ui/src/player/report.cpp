// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/report.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

#include "player/frame.hpp"

namespace diskos::player {
namespace {

using Decoded = std::expected<Report, ReportError>;
using Value = std::expected<std::uint32_t, ReportError>;

constexpr std::size_t kValueDigits = 4;
constexpr std::size_t kPositionDigits = 8;

/** Read a body that is exactly `digits` hex digits; BadBody for anything else. */
Value read_digits(std::string_view body, std::size_t digits) {
    const std::optional<std::uint32_t> value = parse_hex(body);
    if (body.size() != digits || !value) {
        return std::unexpected(ReportError::BadBody);
    }
    return *value;
}

/** Read the 4-digit value most reports consist of; BadValue above `max`. */
Value read_value(std::string_view body, std::uint32_t max = 0xFFFF) {
    return read_digits(body, kValueDigits).and_then([max](std::uint32_t value) -> Value {
        if (value > max) {
            return std::unexpected(ReportError::BadValue);
        }
        return value;
    });
}

bool is_source(std::uint32_t value) {
    constexpr std::array kSources{
            Source::UsbDac,
            Source::Optical,
            Source::BluetoothSink,
            Source::BluetoothStreaming,
            Source::Local,
            Source::Roon,
            Source::AirPlay,
            Source::AirPlayAndDlna,
            Source::Dlna,
    };
    return std::ranges::any_of(kSources, [value](Source source) {
        return static_cast<std::uint32_t>(source) == value;
    });
}

Decoded position(std::string_view body) {
    return read_digits(body, kPositionDigits).transform([](std::uint32_t ms) -> Report {
        return Position{.position_ms = ms};
    });
}

Decoded play_mode(std::string_view body) {
    return read_value(body, static_cast<std::uint32_t>(PlayMode::Single))
            .transform([](std::uint32_t value) -> Report {
                return PlayModeReport{.mode = static_cast<PlayMode>(value)};
            });
}

Decoded volume(std::string_view body) {
    return read_value(body, kMaxVolume).transform([](std::uint32_t value) -> Report {
        return Volume{.level = static_cast<std::uint8_t>(value)};
    });
}

Decoded source(std::string_view body) {
    return read_value(body).and_then([](std::uint32_t value) -> Decoded {
        if (!is_source(value)) {
            return std::unexpected(ReportError::BadValue);
        }
        return SourceReport{.source = static_cast<Source>(value)};
    });
}

Decoded charging(std::string_view body) {
    return read_value(body, 1).transform([](std::uint32_t value) -> Report {
        return Charging{.charging = value == 1};
    });
}

Decoded screen(std::string_view body) {
    return read_value(body, 1).transform([](std::uint32_t value) -> Report {
        return Screen{.on = value == 1};
    });
}

Decoded power_off(std::string_view /*body*/) {
    return PowerOff{};
}

Decoded cover_art(std::string_view body) {
    return read_value(body, 1).transform([](std::uint32_t value) -> Report {
        return CoverArt{.ready = value == 1};
    });
}

Decoded favourite_ack(std::string_view body) {
    return read_value(body).transform([](std::uint32_t value) -> Report {
        return FavouriteReport{.value = static_cast<std::uint16_t>(value)};
    });
}

Decoded battery(std::string_view body) {
    return read_value(body, kMaxBattery).transform([](std::uint32_t value) -> Report {
        return Battery{.percent = static_cast<std::uint8_t>(value)};
    });
}

Decoded conversion_result(std::string_view body) {
    return read_value(body).transform([](std::uint32_t value) -> Report {
        return ConversionResult{.code = static_cast<std::uint16_t>(value)};
    });
}

struct Decoder {
    std::string_view tag;
    Decoded (*decode)(std::string_view body);
};

constexpr std::array kDecoders{
        Decoder{.tag = "a103", .decode = position},
        Decoder{.tag = "a102", .decode = play_mode},
        Decoder{.tag = "a714", .decode = volume},
        Decoder{.tag = "a607", .decode = source},
        Decoder{.tag = "aa1f", .decode = charging},
        Decoder{.tag = "aa23", .decode = screen},
        Decoder{.tag = "aa03", .decode = power_off},
        Decoder{.tag = "aa05", .decode = cover_art},
        Decoder{.tag = "aa01", .decode = favourite_ack},
        Decoder{.tag = "aa1d", .decode = battery},
        Decoder{.tag = "a810", .decode = conversion_result},
};

}  // namespace

std::expected<Report, ReportError> decode_report(std::string_view bytes) {
    const std::expected<Frame, FrameError> frame = parse_frame(bytes);
    if (!frame) {
        return std::unexpected(ReportError::BadFrame);
    }
    const auto* const decoder = std::ranges::find(kDecoders, frame->tag, &Decoder::tag);
    if (decoder == kDecoders.end()) {
        return Unknown{.tag = frame->tag, .body = frame->body};
    }
    return decoder->decode(frame->body);
}

}  // namespace diskos::player

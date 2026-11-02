// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/reports.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>

#include "player/frame.hpp"
#include "utils/numbers.hpp"

namespace diskos::player {
namespace {

using Value = Decoded<std::uint32_t>;

constexpr std::size_t kValueDigits = 4;
constexpr std::size_t kPositionDigits = 8;

/** Read a body that is exactly `digits` hex digits; BadBody for anything else. */
Value read_digits(std::string_view body, std::size_t digits) {
    const std::optional<std::uint32_t> value = utils::parse_hex(body);
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

/** Read a 0/1 flag. */
Decoded<bool> read_flag(std::string_view body) {
    return read_value(body, 1).transform([](std::uint32_t value) { return value == 1; });
}

template <IsReport T>
Decoded<Report> decode_as(std::string_view body) {
    return T::decode(body).transform([](T&& report) -> Report { return std::move(report); });
}

/** a202 has two forms; the full track report starts with its song. */
Decoded<Report> decode_state(std::string_view body) {
    return TrackReport::claims(body) ? decode_as<TrackReport>(body)
                                     : decode_as<PlayStateReport>(body);
}

/** One entry of the tag table: the tag and how to decode its body. */
struct Decoder {
    std::string_view tag;
    Decoded<Report> (*decode)(std::string_view body);
};

template <IsReport T>
constexpr Decoder decoder_for() {
    return {.tag = T::kTag, .decode = decode_as<T>};
}

constexpr std::array kDecoders{
        decoder_for<PositionReport>(),
        decoder_for<PlayModeReport>(),
        decoder_for<VolumeReport>(),
        decoder_for<SourceReport>(),
        decoder_for<ChargingReport>(),
        decoder_for<ScreenReport>(),
        decoder_for<PowerOffReport>(),
        decoder_for<CoverArtReport>(),
        decoder_for<FavouriteReport>(),
        decoder_for<BatteryReport>(),
        decoder_for<ConversionReport>(),
        Decoder{.tag = "a202", .decode = decode_state},
};

// Every report has an entry, except UnknownReport; a202's two forms share one.
constexpr std::size_t kReportTypes = std::variant_size_v<Report>;
static_assert(kDecoders.size() == kReportTypes - 2);

}  // namespace

Decoded<PositionReport> PositionReport::decode(std::string_view body) {
    return read_digits(body, kPositionDigits).transform([](std::uint32_t ms) {
        return PositionReport{.position_ms = ms};
    });
}

Decoded<PlayModeReport> PlayModeReport::decode(std::string_view body) {
    return read_value(body, static_cast<std::uint32_t>(PlayMode::Single))
            .transform([](std::uint32_t value) {
                return PlayModeReport{.mode = static_cast<PlayMode>(value)};
            });
}

Decoded<VolumeReport> VolumeReport::decode(std::string_view body) {
    return read_value(body).and_then([](std::uint32_t value) -> Decoded<VolumeReport> {
        const std::optional<VolumeLevel> level = VolumeLevel::from(value);
        if (!level) {
            return std::unexpected(ReportError::BadValue);
        }
        return VolumeReport{.level = *level};
    });
}

Decoded<SourceReport> SourceReport::decode(std::string_view body) {
    return read_value(body).and_then([](std::uint32_t value) -> Decoded<SourceReport> {
        if (!is_source(value)) {
            return std::unexpected(ReportError::BadValue);
        }
        return SourceReport{.source = static_cast<Source>(value)};
    });
}

Decoded<ChargingReport> ChargingReport::decode(std::string_view body) {
    return read_flag(body).transform([](bool on) { return ChargingReport{.charging = on}; });
}

Decoded<ScreenReport> ScreenReport::decode(std::string_view body) {
    return read_flag(body).transform([](bool on) { return ScreenReport{.on = on}; });
}

Decoded<PowerOffReport> PowerOffReport::decode(std::string_view /*body*/) {
    return PowerOffReport{};
}

Decoded<CoverArtReport> CoverArtReport::decode(std::string_view body) {
    return read_flag(body).transform([](bool ready) { return CoverArtReport{.ready = ready}; });
}

Decoded<FavouriteReport> FavouriteReport::decode(std::string_view body) {
    return read_value(body).transform([](std::uint32_t value) {
        return FavouriteReport{.value = static_cast<std::uint16_t>(value)};
    });
}

Decoded<BatteryReport> BatteryReport::decode(std::string_view body) {
    return read_value(body, kMaxBattery).transform([](std::uint32_t value) {
        return BatteryReport{.percent = static_cast<std::uint8_t>(value)};
    });
}

Decoded<ConversionReport> ConversionReport::decode(std::string_view body) {
    return read_value(body).transform([](std::uint32_t value) {
        return ConversionReport{.code = static_cast<std::uint16_t>(value)};
    });
}

Decoded<PlayStateReport> PlayStateReport::decode(std::string_view body) {
    constexpr std::string_view kHead = R"({"state":)";
    constexpr std::string_view kTail = "}";
    if (!body.starts_with(kHead) || !body.ends_with(kTail)) {
        return std::unexpected(ReportError::BadBody);
    }
    const std::optional<std::int64_t> value = utils::parse_decimal(
            body.substr(kHead.size(), body.size() - kHead.size() - kTail.size())
    );
    if (!value) {
        return std::unexpected(ReportError::BadBody);
    }
    if (*value < 0 || *value > static_cast<std::int64_t>(PlayState::Stopped)) {
        return std::unexpected(ReportError::BadValue);
    }
    return PlayStateReport{.state = static_cast<PlayState>(*value)};
}

Decoded<Report> decode_report(std::string_view bytes) {
    const std::expected<Frame, FrameError> frame = parse_frame(bytes);
    if (!frame) {
        return std::unexpected(ReportError::BadFrame);
    }
    const auto* const decoder = std::ranges::find(kDecoders, frame->tag, &Decoder::tag);
    if (decoder != kDecoders.end()) {
        return decoder->decode(frame->body);
    }
    return UnknownReport{.tag = frame->tag, .body = frame->body};
}

}  // namespace diskos::player

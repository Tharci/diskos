// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstdint>
#include <expected>
#include <string_view>
#include <variant>

#include "player/modes.hpp"

/**
 * What the stock player reports on the /ui queue, decoded from its frames.
 *
 * A fixed-layout report has a 4-hex-digit value as its whole body (position: 8 digits).
 * Reports that carry JSON (a202 state and track, a6xx Bluetooth) are not
 * decoded yet and come back as Unknown.
 */
namespace diskos::player {

inline constexpr std::uint8_t kMaxVolume = 120;  // The device's MAX_VOL; a714 uses the same scale.
inline constexpr std::uint8_t kMaxBattery = 100;

/** a103: the playing position, about once a second while playing, never while paused. */
struct Position {
    std::uint32_t position_ms;
};

/** a102: the play mode the player switched to, after a 0102 command. */
struct PlayModeReport {
    PlayMode mode;
};

/**
 * a714: the volume, after every 0715 command.
 * Over Bluetooth it arrives late late, and in batches.
 **/
struct Volume {
    std::uint8_t level;  // 0..kMaxVolume
};

/** a607: the source the player switched to, after a 0657 command. */
struct SourceReport {
    Source source;
};

/** aa1f: the charger was plugged in or out. */
struct Charging {
    bool charging;
};

/** aa23: the screen went off or on. */
struct Screen {
    bool on;
};

/** aa03: the player is powering off. Its body is unknown (never captured) and ignored. */
struct PowerOff {};

/** aa05: the cover art of the current track was converted (true) or not (false). */
struct CoverArt {
    bool ready;
};

/**
 * aa01: the answer to a 0104 favourite set/unset.
 * What its value means is not known yet.
 * TODO: Test it.
 **/
struct FavouriteReport {
    std::uint16_t value;
};

/** aa1d: the battery level. */
struct Battery {
    std::uint8_t percent;  // 0..kMaxBattery
};

/** a810: the result of a 0810 image conversion. */
struct ConversionResult {
    std::uint16_t code;  // 0 = done; the player cuts longer codes to 4 digits, so -1 is 0xFFFF
};

/**
 * Any other report, with views into the decoded bytes.
 * Includes aa1c, a824 and a60a, which appear around every track
 *  and source change but whose meaning is not known.
 */
struct Unknown {
    std::string_view tag;
    std::string_view body;
};

using Report = std::variant<
        Position,
        PlayModeReport,
        Volume,
        SourceReport,
        Charging,
        Screen,
        PowerOff,
        CoverArt,
        FavouriteReport,
        Battery,
        ConversionResult,
        Unknown>;

enum class ReportError : std::uint8_t {
    BadFrame,  // Not a frame: see parse_frame().
    BadBody,   // The body does not have the report's layout.
    BadValue,  // The value is outside what the report allows.
};

/**
 * Decode one frame received on /ui.
 * Returns:
 *  - the report, for a well-formed frame (Unknown for a tag without a decoder)
 *  - ReportError for anything malformed
 */
[[nodiscard]] std::expected<Report, ReportError> decode_report(std::string_view bytes);

}  // namespace diskos::player

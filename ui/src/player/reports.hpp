// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "player/modes.hpp"

/**
 * What the stock player reports on the /ui queue, decoded from its frames.
 *
 * A fixed-layout report has a 4-hex-digit value as its whole body (position: 8 digits). a202
 * carries the play state or the current track (read by track_report.hpp). The Bluetooth reports
 * (a6xx) are not decoded yet and come back as UnknownReport.
 */
namespace diskos::player {

inline constexpr std::uint8_t kMaxVolume = 120;  // The device's MAX_VOL; a714 uses the same scale.
inline constexpr std::uint8_t kMaxBattery = 100;

/** a103: the playing position, about once a second while playing, never while paused. */
struct PositionReport {
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
struct VolumeReport {
    std::uint8_t level;  // 0..kMaxVolume
};

/** a607: the source the player switched to, after a 0657 command. */
struct SourceReport {
    Source source;
};

/** aa1f: the charger was plugged in or out. */
struct ChargingReport {
    bool charging;
};

/** aa23: the screen went off or on. */
struct ScreenReport {
    bool on;
};

/** aa03: the player is powering off. Its body is unknown (never captured) and ignored. */
struct PowerOffReport {};

/** aa05: the cover art of the current track was converted (true) or not (false). */
struct CoverArtReport {
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
struct BatteryReport {
    std::uint8_t percent;  // 0..kMaxBattery
};

/** a810: the result of a 0810 image conversion. */
struct ConversionReport {
    std::uint16_t code;  // 0 = done; the player cuts longer codes to 4 digits, so -1 is 0xFFFF
};

/** a202, state only. */
struct PlayStateReport {
    PlayState state;
};

/**
 * The current track, as the player describes it.
 * Unknown numbers are 0, missing texts empty.
 **/
struct Song {
    std::int64_t id;      // SONG.ID in the player's library; 0 under AirPlay
    std::int64_t pos_id;  // LIST_SONG_0.ID: the queue row; 0 under AirPlay
    std::string title;    // The file name when the file has no tags or cannot be read
    std::string artist;
    std::string album;
    std::string genre;
    std::string path;  // Empty under AirPlay
    std::int64_t duration_ms;
    std::int64_t sample_rate_hz;
    std::int64_t bits_per_sample;
    std::int64_t bit_rate_kbps;
    std::int64_t track_number;
    std::int64_t channels;
    bool is_sacd;
    bool is_cue;
    bool is_dsd;
    bool is_m3u;
    std::string m3u_path;  // Empty when not from a playlist
};

/** Where the current track is in the player's queue: "3/12" is position 3 of 12. */
struct QueuePosition {
    std::int64_t position;
    std::int64_t count;
};

/** a202, full form. */
struct TrackReport {
    Song song;
    bool favourite;
    std::int64_t player_flag;  // Meaning unknown: 1, 2, 3, 5 and 6 locally, 255 under AirPlay
    PlayState state;           // Can lag: a track that is about to play still reads Stopped
    std::optional<QueuePosition> queue_position;  // None under AirPlay
    std::string work_mode;                        // "LOCAL" or "AIRPLAY" so far
};

/**
 * Any other report, with views into the decoded bytes.
 * Includes aa1c, a824 and a60a, which appear around every track
 *  and source change but whose meaning is not known.
 */
struct UnknownReport {
    std::string_view tag;
    std::string_view body;
};

using Report = std::variant<
        PositionReport,
        PlayModeReport,
        VolumeReport,
        SourceReport,
        ChargingReport,
        ScreenReport,
        PowerOffReport,
        CoverArtReport,
        FavouriteReport,
        BatteryReport,
        ConversionReport,
        PlayStateReport,
        TrackReport,
        UnknownReport>;

/** Why a frame received on /ui could not be decoded. */
enum class ReportError : std::uint8_t {
    BadFrame,  // Not a frame: see parse_frame().
    BadBody,   // The body does not have the report's layout.
    BadValue,  // The value is outside what the report allows.
};

/**
 * Decode one frame received on /ui.
 * Returns:
 *  - the report, for a well-formed frame (UnknownReport for a tag without a decoder)
 *  - ReportError for anything malformed
 */
[[nodiscard]] std::expected<Report, ReportError> decode_report(std::string_view bytes);

}  // namespace diskos::player

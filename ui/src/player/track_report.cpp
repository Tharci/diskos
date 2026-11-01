// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/track_report.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "utils/numbers.hpp"

namespace diskos::player {
namespace {

enum class Slot : std::uint8_t {
    Integer,  // -?(0|[1-9][0-9]*)
    Boolean,  // true or false
    Text,     // Raw bytes, up to the one place where the field's trail follows
    Path,     // Text with every '/' escaped twice, as \\\/
    Plain,    // Up to the next '"', with no '\' or control characters
};

/** A value's slot and the fixed text that follows it. */
struct Field {
    Slot slot;
    std::string_view trail;
};

// The V2.57 template (mq_player format string at 0x6d7c78): this prefix, then one Field per Key,
// in the same order.
constexpr std::string_view kPrefix = R"({"song":"{\"id\":)";

enum class Key : std::uint8_t {
    Id,
    PosId,
    Title,
    Duration,
    Path,
    SampleRate,
    Bits,
    BitRate,
    Artist,
    Album,
    Genre,
    TrackNumber,
    Channels,
    Sacd,
    Cue,
    Dsd,
    M3u,
    M3uPath,
    Favourite,
    PlayerFlag,
    State,
    QueuePosition,
    WorkMode,
};

constexpr std::array kTemplate{
        Field{.slot = Slot::Integer, .trail = R"(,\"pos_id\":)"},
        Field{.slot = Slot::Integer, .trail = R"(,\"song_name\":\")"},
        Field{.slot = Slot::Text, .trail = R"(\",\"song_duration_time\":)"},
        Field{.slot = Slot::Integer, .trail = R"(,\"song_file_path\":\")"},
        Field{.slot = Slot::Path, .trail = R"(\",\"song_sample_rate\":)"},
        Field{.slot = Slot::Integer, .trail = R"(,\"song_encoding_rate\":)"},
        Field{.slot = Slot::Integer, .trail = R"(,\"song_bit_rate\":)"},
        Field{.slot = Slot::Integer, .trail = R"(,\"song_artist_name\":\")"},
        Field{.slot = Slot::Text, .trail = R"(\",\"song_album_name\":\")"},
        Field{.slot = Slot::Text, .trail = R"(\",\"song_style_name\":\")"},
        Field{.slot = Slot::Text, .trail = R"(\",\"song_track\":)"},
        Field{.slot = Slot::Integer, .trail = R"(,\"song_channel\":)"},
        Field{.slot = Slot::Integer, .trail = R"(,\"is_sacd\":)"},
        Field{.slot = Slot::Boolean, .trail = R"(,\"is_cue\":)"},
        Field{.slot = Slot::Boolean, .trail = R"(,\"is_dsd\":)"},
        Field{.slot = Slot::Boolean, .trail = R"(,\"is_m3u\":)"},
        Field{.slot = Slot::Boolean, .trail = R"(,\"m3u_file_path\":\")"},
        Field{.slot = Slot::Text, .trail = R"(\"}","love":)"},
        Field{.slot = Slot::Boolean, .trail = R"(,"playerflag":)"},
        Field{.slot = Slot::Integer, .trail = R"(,"state":)"},
        Field{.slot = Slot::Integer, .trail = R"(,"playing_num":")"},
        Field{.slot = Slot::Plain, .trail = R"(","work_mode":")"},
        Field{.slot = Slot::Plain, .trail = R"("})"},
};
static_assert(kTemplate.size() == static_cast<std::size_t>(Key::WorkMode) + 1);

constexpr std::string_view kNull = "(null)";  // How the player writes a missing text

std::optional<PlayState> to_play_state(std::int64_t value) {
    if (value < 0 || value > static_cast<std::int64_t>(PlayState::Stopped)) {
        return std::nullopt;
    }
    return static_cast<PlayState>(value);
}

/** Undo the path's double escaping: \\\/ is '/', \\\" is '"' and four backslashes are one. */
std::optional<std::string> unescape_path(std::string_view raw) {
    constexpr std::size_t kLayer = 4;  // One backslash, escaped twice
    std::string path;
    std::size_t at = 0;
    while (at < raw.size()) {
        const std::size_t run_end = std::min(raw.find_first_not_of('\\', at), raw.size());
        const std::size_t run = run_end - at;
        at = run_end;
        const bool escapes =
                at < raw.size() && (raw[at] == '/' || raw[at] == '"') && run % kLayer == kLayer - 1;
        if (!escapes && run % kLayer != 0) {
            return std::nullopt;
        }
        path.append(run / kLayer, '\\');
        if (at < raw.size()) {
            path.push_back(raw[at]);
            ++at;
        }
    }
    return path;
}

/** Splits a body into the template's values, as views into the body. */
class TemplateReader {
public:
    explicit TemplateReader(std::string_view body) : body_(body) {}

    /** Read the whole body; nullopt if it does not follow the template exactly. */
    std::optional<std::vector<std::string_view>> read() {
        if (!skip(kPrefix)) {
            return std::nullopt;
        }
        std::vector<std::string_view> values;
        values.reserve(kTemplate.size());
        for (const Field& field : kTemplate) {
            const std::optional<std::string_view> value = read_value(field);
            if (!value || !skip(field.trail)) {
                return std::nullopt;
            }
            values.push_back(*value);
        }
        if (pos_ != body_.size()) {
            return std::nullopt;
        }
        return values;
    }

private:
    [[nodiscard]] std::string_view rest() const {
        return body_.substr(pos_);
    }

    bool skip(std::string_view text) {
        if (!rest().starts_with(text)) {
            return false;
        }
        pos_ += text.size();
        return true;
    }

    /** The next `size` bytes, at most the rest. */
    std::string_view take(std::size_t size) {
        const std::string_view value = rest().substr(0, size);
        pos_ += value.size();
        return value;
    }

    std::optional<std::string_view> read_boolean() {
        for (const std::string_view literal : {"true", "false"}) {
            if (rest().starts_with(literal)) {
                return take(literal.size());
            }
        }
        return std::nullopt;
    }

    std::optional<std::string_view> read_integer() {
        const std::size_t end = rest().find_first_not_of("-0123456789");
        if (!utils::parse_decimal(rest().substr(0, end))) {
            return std::nullopt;
        }
        return take(end);
    }

    std::optional<std::string_view> read_value(const Field& field) {
        switch (field.slot) {
            case Slot::Integer:
                return read_integer();
            case Slot::Boolean:
                return read_boolean();
            case Slot::Text:
            case Slot::Path:
                return read_text(field.trail);
            case Slot::Plain:
                return read_plain();
        }
        return std::nullopt;
    }

    /** Text up to the trail, which must follow exactly once: anything else is ambiguous. */
    std::optional<std::string_view> read_text(std::string_view trail) {
        const std::size_t end = rest().find(trail);
        if (end == std::string_view::npos
            || rest().find(trail, end + 1) != std::string_view::npos) {
            return std::nullopt;
        }
        return take(end);
    }

    std::optional<std::string_view> read_plain() {
        const std::size_t end = rest().find('"');
        const std::string_view text = rest().substr(0, end);
        const bool clean = std::ranges::none_of(text, [](char c) {
            return c == '\\' || static_cast<unsigned char>(c) < 0x20;
        });
        if (end == std::string_view::npos || !clean) {
            return std::nullopt;
        }
        return take(end);
    }

    std::string_view body_;
    std::size_t pos_ = 0;
};

/** Converts the template's values; a value it cannot convert marks the report as failed. */
class FieldReader {
public:
    explicit FieldReader(std::span<const std::string_view> values) : values_(values) {}

    [[nodiscard]] bool failed() const noexcept {
        return failed_;
    }

    std::int64_t integer(Key key) {
        const std::optional<std::int64_t> value = utils::parse_decimal(at(key));
        failed_ |= !value;
        return value.value_or(0);
    }

    [[nodiscard]] bool boolean(Key key) const {
        return at(key) == "true";
    }

    /** A text value; the player's "(null)" for a missing one reads as empty. */
    [[nodiscard]] std::string text(Key key) const {
        return at(key) == kNull ? std::string{} : std::string{at(key)};
    }

    std::string path(Key key) {
        if (at(key) == kNull) {
            return {};
        }
        const std::optional<std::string> path = unescape_path(at(key));
        failed_ |= !path;
        return path.value_or(std::string{});
    }

    PlayState state(Key key) {
        const std::optional<PlayState> state = to_play_state(integer(key));
        failed_ |= !state;
        return state.value_or(PlayState::Stopped);
    }

    /** "position/count", or "(null)" for none. */
    std::optional<QueuePosition> queue_position(Key key) {
        const std::string_view value = at(key);
        if (value == kNull) {
            return std::nullopt;
        }
        const std::size_t slash = value.find('/');
        const std::optional<std::int64_t> position = utils::parse_decimal(value.substr(0, slash));
        const std::optional<std::int64_t> count =
                slash == std::string_view::npos ? std::nullopt
                                                : utils::parse_decimal(value.substr(slash + 1));
        if (!position || !count) {
            failed_ = true;
            return std::nullopt;
        }
        return QueuePosition{.position = *position, .count = *count};
    }

private:
    [[nodiscard]] std::string_view at(Key key) const {
        return values_[static_cast<std::size_t>(key)];
    }

    std::span<const std::string_view> values_;
    bool failed_ = false;
};

Song read_song(FieldReader& fields) {
    return Song{
            .id = fields.integer(Key::Id),
            .pos_id = fields.integer(Key::PosId),
            .title = fields.text(Key::Title),
            .artist = fields.text(Key::Artist),
            .album = fields.text(Key::Album),
            .genre = fields.text(Key::Genre),
            .path = fields.path(Key::Path),
            .duration_ms = fields.integer(Key::Duration),
            .sample_rate_hz = fields.integer(Key::SampleRate),
            .bits_per_sample = fields.integer(Key::Bits),
            .bit_rate_kbps = fields.integer(Key::BitRate),
            .track_number = fields.integer(Key::TrackNumber),
            .channels = fields.integer(Key::Channels),
            .is_sacd = fields.boolean(Key::Sacd),
            .is_cue = fields.boolean(Key::Cue),
            .is_dsd = fields.boolean(Key::Dsd),
            .is_m3u = fields.boolean(Key::M3u),
            .m3u_path = fields.text(Key::M3uPath),
    };
}

}  // namespace

std::expected<TrackReport, ReportError> decode_track_report(std::string_view body) {
    const std::optional<std::vector<std::string_view>> values = TemplateReader{body}.read();
    if (!values) {
        return std::unexpected(ReportError::BadBody);
    }
    FieldReader fields{*values};
    TrackReport report{
            .song = read_song(fields),
            .favourite = fields.boolean(Key::Favourite),
            .player_flag = fields.integer(Key::PlayerFlag),
            .state = fields.state(Key::State),
            .queue_position = fields.queue_position(Key::QueuePosition),
            .work_mode = fields.text(Key::WorkMode),
    };
    if (fields.failed()) {
        return std::unexpected(ReportError::BadValue);
    }
    return report;
}

}  // namespace diskos::player

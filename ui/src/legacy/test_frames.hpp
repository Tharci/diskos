// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
// Test helper: full a202 frames in the V2.57 layout (see player/track_report_test.cpp).
#pragma once

#include <string>
#include <string_view>

#include "player/frame.hpp"

namespace diskos::legacy {

/** The path as the player writes it: every '/' escaped twice. */
inline std::string escaped(std::string_view path) {
    std::string out;
    for (const char c : path) {
        out += c == '/' ? std::string_view{R"(\\\/)"} : std::string_view{&c, 1};
    }
    return out;
}

/** The fields that differ between the frames the tests use. */
struct A202Fields {
    std::string_view ids;  // id and pos_id, as the template writes them
    std::string_view title;
    std::string path;        // As written: escaped, or (null)
    std::string_view flags;  // love and playerflag
    int state;
    std::string_view queue;  // playing_num and work_mode
};

inline std::string a202_frame(const A202Fields& fields) {
    // clang-format off: one template field per line.
    const std::string body =
            R"({"song":"{\"id\":)" + std::string{fields.ids} +
            R"(,\"song_name\":\")" + std::string{fields.title} + R"(\")"
            R"(,\"song_duration_time\":475000)"
            R"(,\"song_file_path\":\")" + fields.path + R"(\")"
            R"(,\"song_sample_rate\":44100)"
            R"(,\"song_encoding_rate\":16)"
            R"(,\"song_bit_rate\":1411)"
            R"(,\"song_artist_name\":\"Some Artist\")"
            R"(,\"song_album_name\":\"Some Album\")"
            R"(,\"song_style_name\":\"Some Genre\")"
            R"(,\"song_track\":1)"
            R"(,\"song_channel\":2)"
            R"(,\"is_sacd\":false)"
            R"(,\"is_cue\":false)"
            R"(,\"is_dsd\":false)"
            R"(,\"is_m3u\":false)"
            R"(,\"m3u_file_path\":\"(null)\"}")"
            R"(,)" + std::string{fields.flags} +
            R"(,"state":)" + std::to_string(fields.state) +
            R"(,)" + std::string{fields.queue} + "}";
    // clang-format on
    return player::build_frame("a202", body);
}

/** A local track report: song 1415 at queue row 1, a favourite, 3rd of 12. */
inline std::string track_frame(std::string_view title, std::string_view path, int state) {
    return a202_frame({
            .ids = R"(1415,\"pos_id\":1)",
            .title = title,
            .path = escaped(path),
            .flags = R"("love":true,"playerflag":5)",
            .state = state,
            .queue = R"("playing_num":"3/12","work_mode":"LOCAL")",
    });
}

/** An AirPlay track report: no ids, no path and no queue position (capture of 2026-11-01). */
inline std::string airplay_frame(std::string_view title, int state) {
    return a202_frame({
            .ids = R"(0,\"pos_id\":0)",
            .title = title,
            .path = "(null)",
            .flags = R"("love":false,"playerflag":255)",
            .state = state,
            .queue = R"f("playing_num":"(null)","work_mode":"AIRPLAY")f",
    });
}

}  // namespace diskos::legacy

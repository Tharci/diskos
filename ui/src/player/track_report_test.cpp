// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/track_report.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <variant>

#include "player/frame.hpp"
#include "player/reports.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::player {
namespace {

// Full a202 frames received from the player on the Disc (capture of 2026-11-01), with the
// titles, names and paths replaced and the length fields recomputed. Non-ASCII bytes are kept.
// One line per template field.

constexpr std::string_view kTrack =
        R"f(a2020275{"song":"{\"id\":1415)f"
        R"f(,\"pos_id\":1)f"
        R"f(,\"song_name\":\"First Song / Part)f"
        "\xE2\x80\x99"
        R"f(s Two\")f"
        R"f(,\"song_duration_time\":475000)f"
        R"f(,\"song_file_path\":\"\\\/tmp\\\/sdcard\\\/Some Artist\\\/)f"
        R"f(2020 - Some Album\\\/Disc - 01\\\/01 - First Song.flac\")f"
        R"f(,\"song_sample_rate\":44100)f"
        R"f(,\"song_encoding_rate\":16)f"
        R"f(,\"song_bit_rate\":1411)f"
        R"f(,\"song_artist_name\":\"Some Artist\")f"
        R"f(,\"song_album_name\":\"Some Album\")f"
        R"f(,\"song_style_name\":\"Some Genre\")f"
        R"f(,\"song_track\":1)f"
        R"f(,\"song_channel\":2)f"
        R"f(,\"is_sacd\":false)f"
        R"f(,\"is_cue\":false)f"
        R"f(,\"is_dsd\":false)f"
        R"f(,\"is_m3u\":false)f"
        R"f(,\"m3u_file_path\":\"(null)\"}")f"
        R"f(,"love":false)f"
        R"f(,"playerflag":5)f"
        R"f(,"state":2)f"
        R"f(,"playing_num":"1/33")f"
        R"f(,"work_mode":"LOCAL"})f";

constexpr std::string_view kFavourite =
        R"f(a2020266{"song":"{\"id\":1416)f"
        R"f(,\"pos_id\":1)f"
        R"f(,\"song_name\":\"Second Song\")f"
        R"f(,\"song_duration_time\":168000)f"
        R"f(,\"song_file_path\":\"\\\/tmp\\\/sdcard\\\/Some Artist\\\/)f"
        R"f(2020 - Some Album\\\/Disc - 01\\\/02 - Second Song.flac\")f"
        R"f(,\"song_sample_rate\":44100)f"
        R"f(,\"song_encoding_rate\":16)f"
        R"f(,\"song_bit_rate\":1411)f"
        R"f(,\"song_artist_name\":\"Some Artist\")f"
        R"f(,\"song_album_name\":\"Some Album\")f"
        R"f(,\"song_style_name\":\"Some Genre\")f"
        R"f(,\"song_track\":2)f"
        R"f(,\"song_channel\":2)f"
        R"f(,\"is_sacd\":false)f"
        R"f(,\"is_cue\":false)f"
        R"f(,\"is_dsd\":false)f"
        R"f(,\"is_m3u\":false)f"
        R"f(,\"m3u_file_path\":\"(null)\"}")f"
        R"f(,"love":true)f"
        R"f(,"playerflag":6)f"
        R"f(,"state":2)f"
        R"f(,"playing_num":"1/2")f"
        R"f(,"work_mode":"LOCAL"})f";

constexpr std::string_view kEmptyFile =
        R"f(a202025F{"song":"{\"id\":1269)f"
        R"f(,\"pos_id\":1)f"
        R"f(,\"song_name\":\"01 - Some File.flac\")f"
        R"f(,\"song_duration_time\":0)f"
        R"f(,\"song_file_path\":\"\\\/tmp\\\/sdcard\\\/Some Folder\\\/)f"
        R"f(2007 - Some Album\\\/01 - Some File.flac\")f"
        R"f(,\"song_sample_rate\":0)f"
        R"f(,\"song_encoding_rate\":0)f"
        R"f(,\"song_bit_rate\":0)f"
        R"f(,\"song_artist_name\":\"Unknown Artist\")f"
        R"f(,\"song_album_name\":\"Unknown album\")f"
        R"f(,\"song_style_name\":\"Unknown genre\")f"
        R"f(,\"song_track\":0)f"
        R"f(,\"song_channel\":0)f"
        R"f(,\"is_sacd\":false)f"
        R"f(,\"is_cue\":false)f"
        R"f(,\"is_dsd\":false)f"
        R"f(,\"is_m3u\":false)f"
        R"f(,\"m3u_file_path\":\"(null)\"}")f"
        R"f(,"love":false)f"
        R"f(,"playerflag":1)f"
        R"f(,"state":2)f"
        R"f(,"playing_num":"1/2302")f"
        R"f(,"work_mode":"LOCAL"})f";

constexpr std::string_view kAirPlayPlaceholder = R"f(a20201FE{"song":"{\"id\":0)f"
                                                 R"f(,\"pos_id\":0)f"
                                                 R"f(,\"song_name\":\"Airplay Playing\")f"
                                                 R"f(,\"song_duration_time\":0)f"
                                                 R"f(,\"song_file_path\":\"(null)\")f"
                                                 R"f(,\"song_sample_rate\":44100)f"
                                                 R"f(,\"song_encoding_rate\":16)f"
                                                 R"f(,\"song_bit_rate\":1411)f"
                                                 R"f(,\"song_artist_name\":\" \")f"
                                                 R"f(,\"song_album_name\":\" \")f"
                                                 R"f(,\"song_style_name\":\"(null)\")f"
                                                 R"f(,\"song_track\":0)f"
                                                 R"f(,\"song_channel\":2)f"
                                                 R"f(,\"is_sacd\":false)f"
                                                 R"f(,\"is_cue\":false)f"
                                                 R"f(,\"is_dsd\":false)f"
                                                 R"f(,\"is_m3u\":false)f"
                                                 R"f(,\"m3u_file_path\":\"(null)\"}")f"
                                                 R"f(,"love":false)f"
                                                 R"f(,"playerflag":255)f"
                                                 R"f(,"state":0)f"
                                                 R"f(,"playing_num":"(null)")f"
                                                 R"f(,"work_mode":"AIRPLAY"})f";

constexpr std::string_view kAirPlay = R"f(a202021E{"song":"{\"id\":0)f"
                                      R"f(,\"pos_id\":0)f"
                                      R"f(,\"song_name\":\"Some Song\")f"
                                      R"f(,\"song_duration_time\":248000)f"
                                      R"f(,\"song_file_path\":\"(null)\")f"
                                      R"f(,\"song_sample_rate\":44100)f"
                                      R"f(,\"song_encoding_rate\":16)f"
                                      R"f(,\"song_bit_rate\":1411)f"
                                      R"f(,\"song_artist_name\":\"Some Artist )f"
                                      "\xE2\x80\xA2"
                                      R"f( )f"
                                      "\xC3\x9C"
                                      R"f(n)f"
                                      "\xC3\xAF"
                                      R"f(code\")f"
                                      R"f(,\"song_album_name\":\"Some Album\")f"
                                      R"f(,\"song_style_name\":\"(null)\")f"
                                      R"f(,\"song_track\":0)f"
                                      R"f(,\"song_channel\":2)f"
                                      R"f(,\"is_sacd\":false)f"
                                      R"f(,\"is_cue\":false)f"
                                      R"f(,\"is_dsd\":false)f"
                                      R"f(,\"is_m3u\":false)f"
                                      R"f(,\"m3u_file_path\":\"(null)\"}")f"
                                      R"f(,"love":false)f"
                                      R"f(,"playerflag":255)f"
                                      R"f(,"state":0)f"
                                      R"f(,"playing_num":"(null)")f"
                                      R"f(,"work_mode":"AIRPLAY"})f";

std::string_view body_of(std::string_view frame) {
    return frame.substr(kHeaderSize);
}

// `text` with the first `from` replaced by `to`.
std::string replaced(std::string_view text, std::string_view from, std::string_view to) {
    std::string result{text};
    const std::size_t at = result.find(from);
    REQUIRE(at != std::string::npos);
    result.replace(at, from.size(), to);
    return result;
}

TEST_CASE("decode_track_report reads a local track", "[player][track]") {
    const auto report = decode_track_report(body_of(kTrack));
    REQUIRE(report.has_value());
    const Song& song = report->song;
    CHECK(song.id == 1415);
    CHECK(song.pos_id == 1);
    CHECK(song.title == "First Song / Part\xE2\x80\x99s Two");
    CHECK(song.artist == "Some Artist");
    CHECK(song.album == "Some Album");
    CHECK(song.genre == "Some Genre");
    CHECK(song.path == "/tmp/sdcard/Some Artist/2020 - Some Album/Disc - 01/01 - First Song.flac");
    CHECK(song.duration_ms == 475000);
    CHECK(song.sample_rate_hz == 44100);
    CHECK(song.bits_per_sample == 16);
    CHECK(song.bit_rate_kbps == 1411);
    CHECK(song.track_number == 1);
    CHECK(song.channels == 2);
    CHECK_FALSE(song.is_sacd);
    CHECK_FALSE(song.is_cue);
    CHECK_FALSE(song.is_dsd);
    CHECK_FALSE(song.is_m3u);
    CHECK(song.m3u_path.empty());
    CHECK_FALSE(report->favourite);
    CHECK(report->player_flag == 5);
    CHECK(report->state == PlayState::Stopped);
    REQUIRE(report->queue_position.has_value());
    CHECK(report->queue_position->position == 1);
    CHECK(report->queue_position->count == 33);
    CHECK(report->work_mode == "LOCAL");
}

TEST_CASE("decode_track_report reads a favourite", "[player][track]") {
    const auto report = decode_track_report(body_of(kFavourite));
    REQUIRE(report.has_value());
    CHECK(report->favourite);
    CHECK(report->song.title == "Second Song");
}

TEST_CASE("decode_track_report reads a file the player could not open", "[player][track]") {
    const auto report = decode_track_report(body_of(kEmptyFile));
    REQUIRE(report.has_value());
    CHECK(report->song.title == "01 - Some File.flac");
    CHECK(report->song.artist == "Unknown Artist");
    CHECK(report->song.duration_ms == 0);
    CHECK(report->song.sample_rate_hz == 0);
    CHECK(report->state == PlayState::Stopped);
}

TEST_CASE("decode_track_report reads AirPlay tracks", "[player][track]") {
    const auto placeholder = decode_track_report(body_of(kAirPlayPlaceholder));
    REQUIRE(placeholder.has_value());
    CHECK(placeholder->song.id == 0);
    CHECK(placeholder->song.title == "Airplay Playing");
    CHECK(placeholder->song.artist == " ");
    CHECK(placeholder->song.path.empty());
    CHECK(placeholder->song.genre.empty());
    CHECK_FALSE(placeholder->queue_position.has_value());
    CHECK(placeholder->player_flag == 255);
    CHECK(placeholder->state == PlayState::Playing);
    CHECK(placeholder->work_mode == "AIRPLAY");

    const auto track = decode_track_report(body_of(kAirPlay));
    REQUIRE(track.has_value());
    CHECK(track->song.artist == "Some Artist \xE2\x80\xA2 \xC3\x9Cn\xC3\xAF" "code");
    CHECK(track->song.duration_ms == 248000);
}

TEST_CASE("decode_track_report keeps raw quotes in a text", "[player][track]") {
    const std::string body = replaced(body_of(kTrack), R"(First Song / Part)", R"(Say "Hi" to)");
    const auto report = decode_track_report(body);
    REQUIRE(report.has_value());
    CHECK(report->song.title == "Say \"Hi\" to\xE2\x80\x99s Two");
}

TEST_CASE("decode_track_report refuses a text whose end is ambiguous", "[player][track]") {
    const std::string body =
            replaced(body_of(kTrack), R"(First Song)", R"(First\",\"song_duration_time\":1 Song)");
    CHECK(decode_track_report(body) == std::unexpected(ReportError::BadBody));
}

TEST_CASE("decode_track_report undoes the path's double escaping", "[player][track]") {
    const std::string body =
            replaced(body_of(kTrack), R"(\\\/tmp\\\/sdcard)", R"(\\\/a\\\\b\\\"c)");
    const auto report = decode_track_report(body);
    REQUIRE(report.has_value());
    CHECK(report->song.path.starts_with(R"(/a\b"c/)"));

    const std::string single = replaced(body_of(kTrack), R"(\\\/tmp)", R"(\/tmp)");
    CHECK(decode_track_report(single) == std::unexpected(ReportError::BadValue));
}

TEST_CASE("decode_track_report refuses anything off the template", "[player][track]") {
    const std::string_view body = body_of(kTrack);
    CHECK(decode_track_report(std::string{body} + " ") == std::unexpected(ReportError::BadBody));
    CHECK(decode_track_report(replaced(body, R"(,\"is_cue\":false)", ""))
          == std::unexpected(ReportError::BadBody));
    CHECK(decode_track_report(replaced(body, R"("playerflag":5)", R"("playerflag":05)"))
          == std::unexpected(ReportError::BadBody));
    CHECK(decode_track_report(replaced(body, R"("love":false)", R"("love":0)"))
          == std::unexpected(ReportError::BadBody));
    CHECK(decode_track_report(replaced(body, R"("LOCAL")", R"("LO\CAL")"))
          == std::unexpected(ReportError::BadBody));
}

TEST_CASE("decode_track_report refuses values out of range", "[player][track]") {
    const std::string_view body = body_of(kTrack);
    CHECK(decode_track_report(replaced(body, R"("state":2)", R"("state":3)"))
          == std::unexpected(ReportError::BadValue));
    CHECK(decode_track_report(replaced(body, R"("1/33")", R"("1-33")"))
          == std::unexpected(ReportError::BadValue));
    CHECK(decode_track_report(replaced(body, R"("1/33")", R"("1/")"))
          == std::unexpected(ReportError::BadValue));
}

TEST_CASE("decode_report decodes the full a202 form", "[player][track]") {
    const auto track = decode_report(kTrack);
    REQUIRE(track.has_value());
    REQUIRE(std::holds_alternative<TrackReport>(*track));
    CHECK(std::get<TrackReport>(*track).song.id == 1415);
}

}  // namespace
}  // namespace diskos::player

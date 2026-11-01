// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <expected>
#include <string_view>

#include "player/reports.hpp"

/**
 * Reads the full form of the a202 report: the current track, sent after a track change or a 0202
 * request. Internal to the player module: decode_report() is the way in.
 *
 * It looks like JSON but is not: the player fills a fixed template with snprintf, so
 * a title holding `"` or `\` arrives unescaped. It is read by that template (stock V2.57 only),
 * strictly: every key in order, integers and booleans exactly, and each text value up to the one
 * place where the next key follows. A value whose end is ambiguous makes the report BadBody.
 */
namespace diskos::player {

/**
 * Decode the body of an a202 frame in its full form.
 * Returns:
 *  - BadBody, if the body does not follow the V2.57 template or a text value's end is ambiguous
 *  - BadValue, for an unknown state, a malformed queue position or an out-of-range number
 */
[[nodiscard]] std::expected<TrackReport, ReportError> decode_track_report(std::string_view body);

}  // namespace diskos::player

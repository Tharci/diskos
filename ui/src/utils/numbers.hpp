// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

/** Strict conversions between numbers and their text form. */
namespace diskos::utils {

/**
 * Read 1 to 8 hex digits (case insensitive).
 * Returns:
 *  - uint32_t - for hex digits
 *  - nullopt - for anything else
 */
[[nodiscard]] std::optional<std::uint32_t> parse_hex(std::string_view digits);

/**
 * Write `value` in uppercase hex, zero-padded to at least `width` digits, like printf's %0*X:
 * a value too wide for `width` keeps all its digits.
 */
[[nodiscard]] std::string format_hex(std::uint64_t value, std::size_t width);

/**
 * Read a whole decimal integer in its canonical form, -?(0|[1-9][0-9]*): no '+', no leading
 * zeros, no spaces. Returns nullopt for anything else or a value that does not fit.
 */
[[nodiscard]] std::optional<std::int64_t> parse_decimal(std::string_view text);

}  // namespace diskos::utils

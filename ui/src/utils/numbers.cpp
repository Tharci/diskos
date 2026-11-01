// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "utils/numbers.hpp"

#include <algorithm>
#include <charconv>
#include <memory>
#include <system_error>

namespace diskos::utils {

std::optional<std::uint32_t> parse_hex(std::string_view digits) {
    constexpr std::size_t kMaxDigits = 8;
    constexpr int kBase = 16;
    if (digits.empty() || digits.size() > kMaxDigits) {
        return std::nullopt;
    }
    const char* const begin = std::to_address(digits.cbegin());
    const char* const end = std::to_address(digits.cend());
    std::uint32_t value = 0;
    const auto [stop, error] = std::from_chars(begin, end, value, kBase);
    if (error != std::errc{} || stop != end) {
        return std::nullopt;
    }
    return value;
}

std::string format_hex(std::uint64_t value, std::size_t width) {
    constexpr std::string_view kDigits = "0123456789ABCDEF";
    constexpr std::uint64_t kBase = 16;
    std::string text;
    for (; value != 0; value /= kBase) {
        const auto digit = static_cast<unsigned>(value % kBase);
        text.push_back(kDigits[digit]);
    }
    text.resize(std::max({text.size(), width, std::size_t{1}}), '0');
    std::ranges::reverse(text);
    return text;
}

std::optional<std::int64_t> parse_decimal(std::string_view text) {
    const std::string_view digits = text.starts_with('-') ? text.substr(1) : text;
    if (digits.empty() || (digits.size() > 1 && digits.front() == '0')) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    const char* const end = std::to_address(text.cend());
    const auto [stop, error] = std::from_chars(std::to_address(text.cbegin()), end, value);
    if (error != std::errc{} || stop != end) {
        return std::nullopt;
    }
    return value;
}

}  // namespace diskos::utils

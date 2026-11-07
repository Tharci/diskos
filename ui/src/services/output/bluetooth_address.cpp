// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/output/bluetooth_address.hpp"

namespace diskos::output {
namespace {

/** The uppercase form of the hex digit `c`, or nullopt if it is not one. */
std::optional<char> upper_hex_digit(char c) noexcept {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
        return c;
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<char>(c - 'a' + 'A');
    }
    return std::nullopt;
}

}  // namespace

std::optional<BluetoothAddress> BluetoothAddress::from(std::string_view text) noexcept {
    constexpr std::size_t kGroupSize = 3;  // Two digits, then a colon
    if (text.size() != kSize) {
        return std::nullopt;
    }
    std::array<char, kSize> canonical{};
    for (std::size_t i = 0; i < kSize; ++i) {
        if (i % kGroupSize == kGroupSize - 1) {
            if (text[i] != ':') {
                return std::nullopt;
            }
            canonical.at(i) = ':';
            continue;
        }
        const std::optional<char> digit = upper_hex_digit(text[i]);
        if (!digit) {
            return std::nullopt;
        }
        canonical.at(i) = *digit;
    }
    return BluetoothAddress{canonical};
}

}  // namespace diskos::output

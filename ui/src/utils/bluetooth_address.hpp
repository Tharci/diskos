// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace diskos::utils {

/** A Bluetooth device address in its canonical form, "AA:BB:CC:DD:EE:FF". */
class BluetoothAddress {
public:
    static constexpr std::size_t kSize = 17;

    /**
     * Get the address `text` spells as six colon-separated pairs of hex digits, in either case.
     * Returns nullopt for anything else.
     */
    [[nodiscard]] static std::optional<BluetoothAddress> from(std::string_view text) noexcept;

    /** Get the address, in uppercase. */
    [[nodiscard]] std::string_view value() const noexcept {
        return {text_.data(), text_.size()};
    }

    friend bool operator==(const BluetoothAddress&, const BluetoothAddress&) = default;

private:
    explicit BluetoothAddress(const std::array<char, kSize>& text) noexcept : text_(text) {}

    std::array<char, kSize> text_;
};

}  // namespace diskos::utils

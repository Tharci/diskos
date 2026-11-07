// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstdint>

namespace diskos::player {

/**
 * One run of the stock player process: 1 for the first one the UI connects to, one more after each
 * restart.
 */
class PlayerInstance {
public:
    explicit constexpr PlayerInstance(std::uint32_t number) noexcept : number_(number) {}

    [[nodiscard]] constexpr std::uint32_t number() const noexcept {
        return number_;
    }

    /** Get the instance that follows this one. */
    [[nodiscard]] constexpr PlayerInstance next() const noexcept {
        return PlayerInstance{number_ + 1};
    }

    friend constexpr bool operator==(PlayerInstance, PlayerInstance) = default;

private:
    std::uint32_t number_;
};

}  // namespace diskos::player

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "utils/synchronized.hpp"

#include <array>
#include <thread>

#include <catch2/catch_test_macros.hpp>

namespace diskos::utils {
namespace {

TEST_CASE("Synchronized gives one thread at a time the value", "[utils][synchronized]") {
    constexpr int kThreads = 4;
    constexpr int kIncrements = 10000;
    Synchronized<int> counter;
    // Unguarded, these increments would race: TSan (preset host-tsan) reports it.
    {
        std::array<std::jthread, kThreads> threads;
        for (std::jthread& thread : threads) {
            thread = std::jthread{[&counter] {
                for (int i = 0; i < kIncrements; ++i) {
                    counter.with([](int& value) { ++value; });
                }
            }};
        }
    }
    const int total = counter.with([](const int& value) { return value; });

    CHECK(total == kThreads * kIncrements);
}

}  // namespace
}  // namespace diskos::utils

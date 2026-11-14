// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include <catch2/catch_test_macros.hpp>

/** Helpers for the platform tests. */
namespace diskos::platform::testing {

/** An empty directory of this test's own, removed afterwards. */
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::string pattern =
                (std::filesystem::temp_directory_path() / "diskos-test-XXXXXX").string();
        REQUIRE(mkdtemp(pattern.data()) != nullptr);
        path_ = pattern;
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

}  // namespace diskos::platform::testing

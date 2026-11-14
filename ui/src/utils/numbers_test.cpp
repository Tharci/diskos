// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "utils/numbers.hpp"

#include <catch2/catch_test_macros.hpp>

namespace diskos::utils {
namespace {

TEST_CASE("parse_hex reads 1 to 8 hex digits and nothing else", "[utils][numbers]") {
    CHECK(parse_hex("0") == 0);
    CHECK(parse_hex("00aB") == 0xAB);
    CHECK(parse_hex("FFFFFFFF") == 0xFFFFFFFF);
    CHECK_FALSE(parse_hex(""));
    CHECK_FALSE(parse_hex("123456789"));
    CHECK_FALSE(parse_hex("-1"));
    CHECK_FALSE(parse_hex("0x1"));
    CHECK_FALSE(parse_hex(" 1"));
    CHECK_FALSE(parse_hex("1 "));
}

TEST_CASE("format_hex pads like %0*X and never truncates", "[utils][numbers]") {
    CHECK(format_hex(0, 4) == "0000");
    CHECK(format_hex(0x2A, 2) == "2A");
    CHECK(format_hex(0x12345, 4) == "12345");
    CHECK(format_hex(0, 0) == "0");
    CHECK(format_hex(0xFFFFFFFF, 8) == "FFFFFFFF");
}

TEST_CASE("parse_decimal reads only the canonical form", "[utils][numbers]") {
    CHECK(parse_decimal("0") == 0);
    CHECK(parse_decimal("475000") == 475000);
    CHECK(parse_decimal("-12") == -12);
    CHECK(parse_decimal("9223372036854775807") == 9223372036854775807);
    CHECK_FALSE(parse_decimal(""));
    CHECK_FALSE(parse_decimal("-"));
    CHECK_FALSE(parse_decimal("01"));
    CHECK_FALSE(parse_decimal("-01"));
    CHECK_FALSE(parse_decimal("+1"));
    CHECK_FALSE(parse_decimal(" 1"));
    CHECK_FALSE(parse_decimal("1 "));
    CHECK_FALSE(parse_decimal("1.5"));
    CHECK_FALSE(parse_decimal("9223372036854775808"));
}

}  // namespace
}  // namespace diskos::utils

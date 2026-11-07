// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/output/bluetooth_address.hpp"

#include <optional>

#include <catch2/catch_test_macros.hpp>

namespace diskos::output {
namespace {

TEST_CASE("BluetoothAddress reads either case into uppercase", "[output][bluetooth_address]") {
    const auto lower = BluetoothAddress::from("a4:c1:38:0f:9e:2b");
    const auto upper = BluetoothAddress::from("A4:C1:38:0F:9E:2B");
    const auto mixed = BluetoothAddress::from("a4:C1:38:0f:9E:2b");

    REQUIRE(lower.has_value());
    CHECK(lower->value() == "A4:C1:38:0F:9E:2B");
    CHECK(lower == upper);
    CHECK(mixed == upper);
}

TEST_CASE(
        "BluetoothAddress rejects anything but six pairs of hex digits",
        "[output][bluetooth_address]"
) {
    CHECK_FALSE(BluetoothAddress::from("").has_value());
    CHECK_FALSE(BluetoothAddress::from("A4:C1:38:0F:9E").has_value());
    CHECK_FALSE(BluetoothAddress::from("A4:C1:38:0F:9E:2B:00").has_value());
    CHECK_FALSE(BluetoothAddress::from("A4:C1:38:0F:9E:2").has_value());
    CHECK_FALSE(BluetoothAddress::from("A4-C1-38-0F-9E-2B").has_value());
    CHECK_FALSE(BluetoothAddress::from("A4_C1_38_0F_9E_2B").has_value());
    CHECK_FALSE(BluetoothAddress::from("A4:C1:38:0F:9E:2G").has_value());
    CHECK_FALSE(BluetoothAddress::from("A4C:1:38:0F:9E:2B").has_value());
    CHECK_FALSE(BluetoothAddress::from("A4:C1:38:0F:9E:2B ").has_value());
}

}  // namespace
}  // namespace diskos::output

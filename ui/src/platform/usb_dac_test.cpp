// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "platform/usb_dac.hpp"

#include <filesystem>
#include <fstream>

#include "platform/test_directory.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::platform {
namespace {

namespace fs = std::filesystem;

TEST_CASE("ProcUsbDac finds a USB audio card by its usbid", "[platform][usb_dac]") {
    const testing::TemporaryDirectory asound;
    ProcUsbDac dac{asound.path().string()};
    fs::create_directory(asound.path() / "card0");  // The internal codec: no usbid
    const bool internal_only = dac.connected();
    fs::create_directory(asound.path() / "card1");
    std::ofstream{asound.path() / "card1" / "usbid"} << "0d8c:0014\n";
    const bool with_usb = dac.connected();

    CHECK_FALSE(internal_only);
    CHECK(with_usb);
}

TEST_CASE("ProcUsbDac finds nothing in a directory it cannot read", "[platform][usb_dac]") {
    ProcUsbDac dac{"/nonexistent/asound"};

    CHECK_FALSE(dac.connected());
}

}  // namespace
}  // namespace diskos::platform

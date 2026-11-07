// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/output/route_plan.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "player/commands.hpp"
#include "services/output/bluetooth_address.hpp"
#include "services/output/output.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::output {
namespace {

using Plan = std::vector<std::string>;

// The sequences as the legacy code sends them (modes.c OUT_SEQ, main.c route_bt), each in its
// full form.
const Plan kToInternalDac{"0666000C0006", "wait 65 ms", "0642000C0000", "0657000C0008"};
const Plan kToSpdif{"0666000C0004", "wait 65 ms", "0642000C0000", "0657000C0008"};
const Plan kToUsbAudio{"0666000C0003", "0642000C0005", "0657000C0008"};
const Plan kToBluetooth{
        "0666000C0006",
        "0642000C0000",
        "0657000C0008",
        "06c1000C0000",
        "0666000C0002",
        "0657000C0008",
};

std::string describe(const SendStep& step) {
    return player::frame(step.command);
}

std::string describe(const WaitStep& step) {
    return "wait " + std::to_string(step.duration.count()) + " ms";
}

/** The steps from `from` to `to`, each as a frame or a description. */
Plan plan(const std::optional<Output>& from, const Output& to) {
    Plan described;
    for (const Step& step : plan_route(from, to)) {
        described.push_back(std::visit([](const auto& typed) { return describe(typed); }, step));
    }
    return described;
}

Output bluetooth(const char* address) {
    const auto device = BluetoothAddress::from(address);
    REQUIRE(device.has_value());
    return Bluetooth{.device = *device};
}

Plan concat(Plan first, const Plan& second) {
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

TEST_CASE(
        "plan_route sends each output's full sequence from an unknown one", "[output][route_plan]"
) {
    const Output speaker = bluetooth("A4:C1:38:0F:9E:2B");

    CHECK(plan(std::nullopt, InternalDac{}) == kToInternalDac);
    CHECK(plan(std::nullopt, Spdif{}) == kToSpdif);
    CHECK(plan(std::nullopt, UsbAudio{}) == kToUsbAudio);
    CHECK(plan(std::nullopt, speaker) == kToBluetooth);
}

TEST_CASE("plan_route sends the same sequence between wired outputs", "[output][route_plan]") {
    CHECK(plan(InternalDac{}, Spdif{}) == kToSpdif);
    CHECK(plan(Spdif{}, InternalDac{}) == kToInternalDac);
    CHECK(plan(InternalDac{}, UsbAudio{}) == kToUsbAudio);
    CHECK(plan(UsbAudio{}, InternalDac{}) == kToInternalDac);
    CHECK(plan(UsbAudio{}, Spdif{}) == kToSpdif);
}

TEST_CASE("plan_route leaves Bluetooth through the internal DAC", "[output][route_plan]") {
    const Output speaker = bluetooth("A4:C1:38:0F:9E:2B");

    CHECK(plan(speaker, InternalDac{}) == kToInternalDac);
    CHECK(plan(speaker, Spdif{}) == concat(kToInternalDac, kToSpdif));
    CHECK(plan(speaker, UsbAudio{}) == concat(kToInternalDac, kToUsbAudio));
}

TEST_CASE("plan_route routes from any output to a Bluetooth sink alike", "[output][route_plan]") {
    const Output speaker = bluetooth("A4:C1:38:0F:9E:2B");
    const Output headphones = bluetooth("00:1B:66:C2:7D:01");

    CHECK(plan(InternalDac{}, speaker) == kToBluetooth);
    CHECK(plan(UsbAudio{}, speaker) == kToBluetooth);
    CHECK(plan(headphones, speaker) == kToBluetooth);
}

TEST_CASE("plan_route plans nothing to the output already applied", "[output][route_plan]") {
    const Output speaker = bluetooth("A4:C1:38:0F:9E:2B");
    const Output same_speaker = bluetooth("a4:c1:38:0f:9e:2b");

    CHECK(plan(InternalDac{}, InternalDac{}).empty());
    CHECK(plan(UsbAudio{}, UsbAudio{}).empty());
    CHECK(plan(speaker, same_speaker).empty());
}

}  // namespace
}  // namespace diskos::output

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <variant>

#include "utils/bluetooth_address.hpp"

namespace diskos::output {

/** The internal DAC and its headphone jack. */
struct InternalDac {
    friend bool operator==(const InternalDac&, const InternalDac&) = default;
};

/** The S/PDIF output. */
struct Spdif {
    friend bool operator==(const Spdif&, const Spdif&) = default;
};

/** An external USB DAC on the USB port, with the Disc as the USB host. */
struct UsbAudio {
    friend bool operator==(const UsbAudio&, const UsbAudio&) = default;
};

/** A Bluetooth sink: a speaker or headphones. */
struct Bluetooth {
    utils::BluetoothAddress device;

    friend bool operator==(const Bluetooth&, const Bluetooth&) = default;
};

/** Where the player sends its audio. */
using Output = std::variant<InternalDac, Spdif, UsbAudio, Bluetooth>;

}  // namespace diskos::output

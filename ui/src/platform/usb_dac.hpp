// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <string>
#include <utility>

#include "services/output/interfaces.hpp"

namespace diskos::platform {

/**
 * Finds a USB DAC through ALSA's /proc entries: the USB audio driver gives each card it creates a
 * `usbid` file (/proc/asound/cardN/usbid), and no other card has one. It looks at every card
 * number ALSA can give. Not yet seen on the Disc's
 * kernel: the legacy check (card1 only) is marked unverified too.
 */
class ProcUsbDac final : public output::UsbDac {
public:
    /** Look for USB cards under `asound`, ALSA's /proc directory. */
    explicit ProcUsbDac(std::string asound = "/proc/asound") : asound_(std::move(asound)) {}

    /** Check whether ALSA has a USB audio card. Returns false if the directory cannot be read. */
    [[nodiscard]] bool connected() override;

private:
    std::string asound_;
};

}  // namespace diskos::platform

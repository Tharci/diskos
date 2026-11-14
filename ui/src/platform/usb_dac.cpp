// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "platform/usb_dac.hpp"

#include <string>

#include <unistd.h>

namespace diskos::platform {
namespace {

// ALSA numbers its cards below CONFIG_SND_MAX_CARDS, 32 unless the kernel is built otherwise.
constexpr int kMaxCards = 32;

}  // namespace

bool ProcUsbDac::connected() {
    for (int card = 0; card < kMaxCards; ++card) {
        const std::string usbid = asound_ + "/card" + std::to_string(card) + "/usbid";
        if (access(usbid.c_str(), F_OK) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace diskos::platform

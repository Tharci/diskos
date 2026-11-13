// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/output.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include "legacy/ipc_command_sender.hpp"
#include "legacy/player_controller.hpp"
#include "legacy/player_link.hpp"
#include "legacy/player_state.h"
#include "legacy/player_state.hpp"
#include "platform/route_store.hpp"
#include "platform/usb_dac.hpp"
#include "services/output/bluetooth_address.hpp"
#include "services/output/interfaces.hpp"
#include "services/output/output.hpp"
#include "services/output/output_router.hpp"
#include "services/playback/player_controller.hpp"
#include "services/playback/player_state.hpp"

namespace diskos::legacy {
namespace {

using output::Clock;
using output::Output;

/** Local playback as the router sees it: the legacy controller, and the local PlayerState. */
class LegacyPlayback final : public output::PlaybackControl {
public:
    playback::PlayState play_state() override {
        return player_state(DISKOS_PLAYER_LOCAL).with([](const playback::PlayerState& state) {
            return state.play_state();
        });
    }

    std::optional<playback::PlaybackSnapshot> snapshot() override {
        return player_controller().snapshot();
    }

    output::SendResult pause(Clock::time_point now) override {
        return player_controller().pause(now);
    }

    void note_output_reset(Clock::time_point now) override {
        player_controller().note_output_reset(now);
    }

    output::SendResult restore(
            const playback::PlaybackSnapshot& snapshot,
            playback::Resume resume,
            Clock::time_point now
    ) override {
        return player_controller().restore(snapshot, resume, now);
    }
};

/** What blocks the router, as the legacy C says. */
class HookAdmission final : public output::Admission {
public:
    explicit HookAdmission(diskos_output_blocker (*ask)()) : ask_(ask) {}

    std::optional<output::Blocker> blocker() override {
        using output::Blocker;
        switch (ask_()) {
            case DISKOS_OUTPUT_UNBLOCKED:
                return std::nullopt;
            case DISKOS_OUTPUT_CARD_NOT_READY:
                return Blocker::CardNotReady;
            case DISKOS_OUTPUT_OTHER_SOURCE:
                return Blocker::OtherSource;
            case DISKOS_OUTPUT_AUDIOBOOK:
                return Blocker::Audiobook;
            case DISKOS_OUTPUT_PLAY_STARTING:
                return Blocker::PlayStarting;
            case DISKOS_OUTPUT_CASTING:
                return Blocker::Casting;
        }
        return Blocker::CardNotReady;  // Not a blocker this code knows: hold everything
    }

private:
    diskos_output_blocker (*ask_)();  // The legacy C's answer
};

/** The router and everything it works with, for the whole run. */
class Bridge {
public:
    explicit Bridge(diskos_output_hooks hooks) : hooks_(hooks), admission_(hooks.blocker) {}

    Bridge(const Bridge&) = delete;
    Bridge(Bridge&&) = delete;
    Bridge& operator=(const Bridge&) = delete;
    Bridge& operator=(Bridge&&) = delete;
    ~Bridge() = default;

    [[nodiscard]] output::OutputRouter& router() noexcept {
        return router_;
    }

    [[nodiscard]] const diskos_output_hooks& hooks() const noexcept {
        return hooks_;
    }

private:
    diskos_output_hooks hooks_;
    IpcCommandSender sender_{IpcCommandSender::Policy::Background};
    LegacyPlayback playback_{};
    platform::ProcUsbDac usb_dac_{};
    HookAdmission admission_;
    platform::FileRouteStore store_{};
    output::OutputRouter router_{sender_, playback_, usb_dac_, admission_, store_};
};

/** The started bridge, or nullptr before diskos_output_start(). Main thread only. */
Bridge*& started() {
    static Bridge* bridge = nullptr;
    return bridge;
}

/** Write `address` into the caller's `mac` (DISKOS_OUTPUT_MAC_SIZE bytes, or nullptr),
 * NUL-terminated. */
void write_mac(char* mac, std::string_view address) {
    if (mac == nullptr) {
        return;
    }
    const std::span<char> out{mac, DISKOS_OUTPUT_MAC_SIZE};
    std::ranges::fill(out, '\0');
    std::ranges::copy(address.substr(0, out.size() - 1), out.begin());
}

diskos_output_kind kind_of(const std::optional<Output>& output, char* mac) {
    const auto* bluetooth = output ? std::get_if<output::Bluetooth>(&*output) : nullptr;
    write_mac(mac, bluetooth != nullptr ? bluetooth->device.value() : std::string_view{});
    if (!output) {
        return DISKOS_OUTPUT_UNKNOWN;
    }
    if (bluetooth != nullptr) {
        return DISKOS_OUTPUT_BLUETOOTH;
    }
    if (std::holds_alternative<output::Spdif>(*output)) {
        return DISKOS_OUTPUT_SPDIF;
    }
    return std::holds_alternative<output::UsbAudio>(*output) ? DISKOS_OUTPUT_USB_AUDIO
                                                             : DISKOS_OUTPUT_INTERNAL_DAC;
}

std::optional<Output> output_of(diskos_output_kind kind, const char* mac) {
    switch (kind) {
        case DISKOS_OUTPUT_INTERNAL_DAC:
            return output::InternalDac{};
        case DISKOS_OUTPUT_SPDIF:
            return output::Spdif{};
        case DISKOS_OUTPUT_USB_AUDIO:
            return output::UsbAudio{};
        case DISKOS_OUTPUT_BLUETOOTH: {
            const auto device = output::BluetoothAddress::from(mac != nullptr ? mac : "");
            return device ? std::optional<Output>{output::Bluetooth{.device = *device}}
                          : std::nullopt;
        }
        case DISKOS_OUTPUT_UNKNOWN:
            return std::nullopt;
    }
    return std::nullopt;
}

diskos_output_selection selection_of(output::Refusal refusal) {
    switch (refusal) {
        case output::Refusal::CardNotReady:
            return DISKOS_OUTPUT_REFUSED_CARD_NOT_READY;
        case output::Refusal::OtherSource:
            return DISKOS_OUTPUT_REFUSED_OTHER_SOURCE;
        case output::Refusal::Audiobook:
            return DISKOS_OUTPUT_REFUSED_AUDIOBOOK;
        case output::Refusal::NoUsbDac:
            return DISKOS_OUTPUT_REFUSED_NO_USB_DAC;
    }
    return DISKOS_OUTPUT_REFUSED_INVALID;
}

diskos_output_outcome outcome_of(output::Outcome::Kind kind) {
    using enum output::Outcome::Kind;
    switch (kind) {
        case Switched:
            return DISKOS_OUTPUT_SWITCHED;
        case Retrying:
            return DISKOS_OUTPUT_RETRYING;
        case Failed:
            return DISKOS_OUTPUT_FAILED;
        case NoUsbDac:
            return DISKOS_OUTPUT_NO_USB_DAC;
        case Recovering:
            return DISKOS_OUTPUT_RECOVERING;
        case ResumeFailed:
            return DISKOS_OUTPUT_RESUME_FAILED;
    }
    return DISKOS_OUTPUT_FAILED;
}

void tick(Bridge& bridge) {
    const Clock::time_point now = Clock::now();
    if (platform::PlayerLink* const link = player_link()) {
        const std::size_t dropped = link->deliver(bridge.router(), now);
        if (dropped > 0) {
            const std::string line =
                    "output: " + std::to_string(dropped) + " player frames dropped\n";
            std::fputs(line.c_str(), stderr);
        }
    }
    player_controller().advance(now);
    for (const output::Outcome& outcome : bridge.router().advance(now)) {
        bridge.hooks().on_outcome(outcome_of(outcome.kind), kind_of(outcome.output, nullptr));
    }
}

/** Note that `device` is gone, then tick, so a Departure sends its first 0666 at once. */
void lose(Bridge& bridge, const output::BluetoothAddress& device) {
    bridge.router().bluetooth_lost(device);
    tick(bridge);
}

}  // namespace
}  // namespace diskos::legacy

extern "C" void diskos_output_start(diskos_output_hooks hooks) {
    using diskos::legacy::Bridge;
    if (diskos::legacy::started() == nullptr) {
        static Bridge bridge{hooks};
        diskos::legacy::started() = &bridge;
    }
}

extern "C" void diskos_output_tick(void) {
    if (auto* const bridge = diskos::legacy::started()) {
        diskos::legacy::tick(*bridge);
    }
}

extern "C" diskos_output_selection diskos_output_select(
        diskos_output_kind output, const char* mac
) {
    using namespace diskos::legacy;
    Bridge* const bridge = started();
    const auto target = output_of(output, mac);
    if (bridge == nullptr || !target) {
        return DISKOS_OUTPUT_REFUSED_INVALID;
    }
    const auto selected = bridge->router().select(*target);
    return selected ? DISKOS_OUTPUT_SELECTED : selection_of(selected.error());
}

extern "C" void diskos_output_bluetooth_connected(const char* mac) {
    using namespace diskos::legacy;
    Bridge* const bridge = started();
    const auto device = diskos::output::BluetoothAddress::from(mac != nullptr ? mac : "");
    if (bridge != nullptr && device) {
        bridge->router().bluetooth_connected(*device);
    }
}

extern "C" void diskos_output_bluetooth_lost(const char* mac) {
    using namespace diskos::legacy;
    Bridge* const bridge = started();
    const auto device = diskos::output::BluetoothAddress::from(mac != nullptr ? mac : "");
    if (bridge != nullptr && device) {
        lose(*bridge, *device);
    }
}

extern "C" void diskos_output_leave_bluetooth(void) {
    using namespace diskos::legacy;
    Bridge* const bridge = started();
    if (bridge == nullptr) {
        return;
    }
    const diskos::output::OutputRouter& router = bridge->router();
    for (const auto& output : {router.applied(), std::optional<Output>{router.desired()}}) {
        if (const auto* sink =
                    output ? std::get_if<diskos::output::Bluetooth>(&*output) : nullptr) {
            lose(*bridge, sink->device);
            return;
        }
    }
}

extern "C" void diskos_output_source_changed(void) {
    if (auto* const bridge = diskos::legacy::started()) {
        bridge->router().source_changed();
    }
}

extern "C" int diskos_output_prepare_play(void) {
    auto* const bridge = diskos::legacy::started();
    return bridge == nullptr || bridge->router().prepare_play() ? 0 : -1;
}

extern "C" diskos_output_kind diskos_output_desired(char* mac) {
    auto* const bridge = diskos::legacy::started();
    using diskos::output::Output;
    const std::optional<Output> desired =
            bridge != nullptr ? std::optional<Output>{bridge->router().desired()} : std::nullopt;
    return diskos::legacy::kind_of(desired, mac);
}

extern "C" diskos_output_kind diskos_output_applied(char* mac) {
    auto* const bridge = diskos::legacy::started();
    return diskos::legacy::kind_of(
            bridge != nullptr ? bridge->router().applied() : std::nullopt, mac
    );
}

extern "C" int diskos_output_busy(void) {
    auto* const bridge = diskos::legacy::started();
    return bridge != nullptr && bridge->router().busy() ? 1 : 0;
}

extern "C" int diskos_output_ready(void) {
    auto* const bridge = diskos::legacy::started();
    return bridge != nullptr && bridge->router().ready() ? 1 : 0;
}

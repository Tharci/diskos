// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/output/route_plan.hpp"

#include <utility>

namespace diskos::output {
namespace {

using player::OutputRoute;
using player::RouteOutputCommand;
using player::SelectUsbGadgetCommand;
using player::UsbGadget;

constexpr std::chrono::milliseconds kRouteSettle{65};  // Between 0666 and 0642, as stock waits
constexpr player::SwitchSourceCommand kLocalSource{.source = player::Source::Local};

/** Plans the route to one kind of output: each overload appends its sequence. */
class Planner {
public:
    explicit Planner(const std::optional<Output>& from)
        : from_bluetooth_(from && std::holds_alternative<Bluetooth>(*from)) {}

    void operator()(const InternalDac& /*to*/) {
        wired(OutputRoute::InternalDac);
    }

    void operator()(const Spdif& /*to*/) {
        leave_bluetooth();
        wired(OutputRoute::Spdif);
    }

    void operator()(const UsbAudio& /*to*/) {
        leave_bluetooth();
        send(RouteOutputCommand{.route = OutputRoute::UsbHostAudio});
        send(SelectUsbGadgetCommand{.gadget = UsbGadget::UsbHostAudio});
        send(kLocalSource);
    }

    void operator()(const Bluetooth& /*to*/) {
        send(RouteOutputCommand{.route = OutputRoute::InternalDac});
        send(SelectUsbGadgetCommand{.gadget = UsbGadget::None});
        send(kLocalSource);
        send(player::EnableBluetoothCommand{});
        send(RouteOutputCommand{.route = OutputRoute::BluetoothSource});
        send(kLocalSource);
    }

    /** Take the planned steps. */
    [[nodiscard]] std::vector<Step> steps() && {
        return std::move(steps_);
    }

private:
    void send(player::Command command) {
        steps_.emplace_back(SendStep{.command = std::move(command)});
    }

    void wait(std::chrono::milliseconds duration) {
        steps_.emplace_back(WaitStep{.duration = duration});
    }

    /** The sequence to the internal DAC or S/PDIF. */
    void wired(OutputRoute route) {
        send(RouteOutputCommand{.route = route});
        wait(kRouteSettle);
        send(SelectUsbGadgetCommand{.gadget = UsbGadget::None});
        send(kLocalSource);
    }

    /** The way off a Bluetooth sink first, if the route starts on one. */
    void leave_bluetooth() {
        if (from_bluetooth_) {
            wired(OutputRoute::InternalDac);
        }
    }

    bool from_bluetooth_;
    std::vector<Step> steps_{};
};

}  // namespace

std::vector<Step> plan_route(const std::optional<Output>& from, const Output& to) {
    if (from == to) {
        return {};
    }
    Planner planner{from};
    std::visit(planner, to);
    return std::move(planner).steps();
}

}  // namespace diskos::output

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "platform/player_link.hpp"

#include <atomic>
#include <chrono>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "platform/message_queue.hpp"
#include "player/commands.hpp"
#include "player/frame_listener.hpp"
#include "player/frame_sender.hpp"
#include "player/player_instance.hpp"
#include "player/report_listener.hpp"
#include "player/reports.hpp"
#include "utils/synchronized.hpp"
#include <catch2/catch_test_macros.hpp>
#include <mqueue.h>
#include <unistd.h>

namespace diskos::platform {
namespace {

using namespace std::chrono_literals;
using player::PlayerInstance;
using player::SendError;

// Fast enough for tests: every wait below is a small multiple of these.
constexpr LinkTiming kTiming{.receive_wait = 20ms, .restart_check = 50ms, .retry_wait = 5ms};
constexpr QueueCapacity kCapacity{.max_messages = 4, .message_size = 1024};

/** Queue names of this test's own, so tests never meet the device's or each other's queues. */
QueueNames unique_names() {
    static std::atomic<int> counter = 0;
    const std::string prefix =
            "/diskos-test-" + std::to_string(getpid()) + "-" + std::to_string(counter++);
    return {.reports = prefix + "-ui", .commands = prefix + "-player"};
}

/** Wait until `done` returns true, or give up after a second. */
bool eventually(const auto& done) {
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

/** Records what the link delivers, one line per call: "connected 1", the frame, "drained". */
class RecordingListener final : public player::FrameListener {
public:
    void on_connected(PlayerInstance instance) override {
        add("connected " + std::to_string(instance.number()));
    }

    void on_frame(std::string_view frame) override {
        add(std::string{frame});
    }

    void on_drained() override {
        add("drained");
    }

    [[nodiscard]] std::vector<std::string> events() {
        return events_.with([](const std::vector<std::string>& events) { return events; });
    }

    /** Wait until `count` calls arrived, and get them. */
    [[nodiscard]] std::vector<std::string> wait_for(std::size_t count) {
        (void)eventually([&] { return events().size() >= count; });
        return events();
    }

private:
    void add(std::string event) {
        events_.with([&event](std::vector<std::string>& events) {
            events.push_back(std::move(event));
        });
    }

    utils::Synchronized<std::vector<std::string>> events_;
};

/** The player's side of the queues: it creates them, sends reports and reads commands. */
class FakePlayer {
public:
    explicit FakePlayer(QueueNames names) : names_(std::move(names)) {}

    FakePlayer(const FakePlayer&) = delete;
    FakePlayer& operator=(const FakePlayer&) = delete;

    ~FakePlayer() {
        mq_unlink(names_.reports.c_str());
        mq_unlink(names_.commands.c_str());
    }

    /** Create both queues anew, as the player does when it starts. */
    void start() {
        mq_unlink(names_.reports.c_str());
        mq_unlink(names_.commands.c_str());
        reports_owner_ = *MessageReceiver::open_or_create(names_.reports, kCapacity);
        commands_ = *MessageReceiver::open_or_create(names_.commands, kCapacity);
        reports_ = *MessageSender::open(names_.reports);
    }

    void report(std::string_view frame) {
        REQUIRE((reports_ ? reports_->send(frame).has_value() : false));
    }

    /** Get the next command the UI sent, or nullopt if none came. */
    std::optional<std::string> command() {
        std::vector<char> buffer(static_cast<std::size_t>(kCapacity.message_size));
        const auto size = commands_ ? commands_->receive(buffer, 500ms)
                                    : std::unexpected(MessageReceiver::Error::Closed);
        return size ? std::optional{std::string{buffer.data(), *size}} : std::nullopt;
    }

private:
    QueueNames names_;
    std::optional<MessageReceiver> reports_owner_;  // Keeps the queue the player owns
    std::optional<MessageSender> reports_;
    std::optional<MessageReceiver> commands_;
};

TEST_CASE("PlayerLink creates the reports queue when the player has not", "[platform][link]") {
    const QueueNames names = unique_names();
    FakePlayer player{names};
    RecordingListener listener;
    PlayerLink link{listener, names, kTiming};
    const auto connected = listener.wait_for(1);

    CHECK(connected == std::vector<std::string>{"connected 1"});
    CHECK(link.instance() == PlayerInstance{1});
}

TEST_CASE("PlayerLink delivers the player's frames in order", "[platform][link]") {
    const QueueNames names = unique_names();
    FakePlayer player{names};
    player.start();
    RecordingListener listener;
    PlayerLink link{listener, names, kTiming};
    (void)listener.wait_for(1);
    player.report("a103001000C9174D");
    player.report(R"(a2020013{"state":1})");
    const auto events = listener.wait_for(3);

    CHECK(events
          == std::vector<std::string>{"connected 1", "a103001000C9174D", R"(a2020013{"state":1})"});
}

/** Records the reports deliver() hands it, on the test's thread: "connected 1", "a103", ... */
class RecordingReportListener final : public player::ReportListener {
public:
    void on_connected(PlayerInstance instance, Clock::time_point /*now*/) override {
        events_.push_back("connected " + std::to_string(instance.number()));
    }

    void on_report(const player::Report& report, Clock::time_point /*now*/) override {
        events_.emplace_back(std::visit([](const auto& typed) { return tag_of(typed); }, report));
    }

    [[nodiscard]] const std::vector<std::string>& events() const noexcept {
        return events_;
    }

private:
    static std::string tag_of(const player::UnknownReport& report) {
        return std::string{report.tag};
    }

    template <typename Report>
    static std::string tag_of(const Report& /*report*/) {
        return std::string{Report::kTag};
    }

    std::vector<std::string> events_{};
};

TEST_CASE(
        "PlayerLink delivers what it received, decoded, on the caller's thread", "[platform][link]"
) {
    const QueueNames names = unique_names();
    FakePlayer player{names};
    player.start();
    RecordingListener frames;
    PlayerLink link{frames, names, kTiming};
    (void)frames.wait_for(1);
    player.report("a103001000C9174D");
    player.report("not a frame");
    player.report(R"(a2020013{"state":1})");
    (void)frames.wait_for(4);
    RecordingReportListener reports;
    const std::size_t dropped = link.deliver(reports, std::chrono::steady_clock::now());
    const std::vector<std::string> expected{"connected 1", "a103", "a202"};

    CHECK(dropped == 0);
    CHECK(reports.events() == expected);
}

TEST_CASE("PlayerLink sends to the connected instance, also commands", "[platform][link]") {
    const QueueNames names = unique_names();
    FakePlayer player{names};
    player.start();
    RecordingListener listener;
    PlayerLink link{listener, names, kTiming};
    (void)listener.wait_for(1);
    player::FrameCommandSender commands{link};
    const auto toggled = commands.send(player::PlayPauseCommand{});
    const auto probed = link.send_frame("02020008", PlayerInstance{1});
    const auto misdirected = link.send_frame("02020008", PlayerInstance{2});

    CHECK(toggled.has_value());
    CHECK(probed.has_value());
    CHECK(misdirected == std::unexpected(SendError::WrongInstance));
    CHECK(player.command() == "0201000C0000");
    CHECK(player.command() == "02020008");
    CHECK(player.command() == std::nullopt);
}

TEST_CASE("PlayerLink fails a send when the player has no commands queue", "[platform][link]") {
    const QueueNames names = unique_names();
    FakePlayer player{names};
    RecordingListener listener;
    PlayerLink link{listener, names, kTiming};

    CHECK(link.send_frame("0201000C0000") == std::unexpected(SendError::Failed));
}

TEST_CASE("PlayerLink retries a full commands queue, then fails", "[platform][link]") {
    const QueueNames names = unique_names();
    FakePlayer player{names};
    player.start();
    RecordingListener listener;
    PlayerLink link{listener, names, kTiming};
    for (long sent = 0; sent < kCapacity.max_messages; ++sent) {
        REQUIRE(link.send_frame("02020008").has_value());
    }
    const auto full = link.send_frame("0201000C0000");
    (void)player.command();
    const auto drained = link.send_frame("0201000C0000");

    CHECK(full == std::unexpected(SendError::Failed));
    CHECK(drained.has_value());
}

TEST_CASE("PlayerLink follows a player restart", "[platform][link]") {
    const QueueNames names = unique_names();
    FakePlayer player{names};
    player.start();
    RecordingListener listener;
    PlayerLink link{listener, names, kTiming};
    (void)listener.wait_for(1);
    player.start();  // The restarted player creates both queues anew
    const auto reconnected = listener.wait_for(2);
    player.report("aa1d000C0057");
    const auto events = listener.wait_for(3);
    const auto sent = link.send_frame("0201000C0001");
    const auto to_old = link.send_frame("02020008", PlayerInstance{1});

    CHECK(reconnected == std::vector<std::string>{"connected 1", "connected 2"});
    CHECK(events.back() == "aa1d000C0057");
    CHECK(sent.has_value());
    CHECK(player.command() == "0201000C0001");
    CHECK(to_old == std::unexpected(SendError::WrongInstance));
}

TEST_CASE("PlayerLink drains on request", "[platform][link]") {
    const QueueNames names = unique_names();
    FakePlayer player{names};
    player.start();
    RecordingListener listener;
    PlayerLink link{listener, names, kTiming};
    (void)listener.wait_for(1);
    player.report("aa23000C0000");
    player.report("aa23000C0001");
    link.request_drain();
    const auto events = listener.wait_for(4);

    CHECK(events
          == std::vector<std::string>{"connected 1", "aa23000C0000", "aa23000C0001", "drained"});
}

}  // namespace
}  // namespace diskos::platform

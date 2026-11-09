// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "platform/route_store.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "platform/message_queue.hpp"
#include "platform/test_directory.hpp"
#include "services/output/bluetooth_address.hpp"
#include "services/output/output.hpp"
#include <catch2/catch_test_macros.hpp>
#include <mqueue.h>
#include <sys/stat.h>
#include <unistd.h>

namespace diskos::platform {
namespace {

namespace fs = std::filesystem;
using output::Output;
using output::StoredRoute;

/** A reports queue of this test's own, standing for the running player; removed afterwards. */
class TestQueue {
public:
    TestQueue() {
        static std::atomic<int> counter = 0;
        name_ = "/diskos-test-" + std::to_string(getpid()) + "-store-" + std::to_string(counter++);
        create();
    }

    TestQueue(const TestQueue&) = delete;
    TestQueue(TestQueue&&) = delete;
    TestQueue& operator=(const TestQueue&) = delete;
    TestQueue& operator=(TestQueue&&) = delete;

    ~TestQueue() {
        mq_unlink(name_.c_str());
    }

    /** Recreate the queue, as a new player does. */
    void recreate() {
        mq_unlink(name_.c_str());
        create();
    }

    [[nodiscard]] const std::string& name() const noexcept {
        return name_;
    }

    [[nodiscard]] QueueIdentity identity() const {
        REQUIRE(receivers_.back().identity().has_value());
        return *receivers_.back().identity();
    }

private:
    void create() {
        auto created =
                MessageReceiver::open_or_create(name_, {.max_messages = 4, .message_size = 64});
        REQUIRE(created.has_value());
        receivers_.push_back(std::move(*created));  // The old ones stay open: no inode is reused
    }

    std::string name_;
    std::vector<MessageReceiver> receivers_{};
};

/** A store in a directory of its own, for the test queue's player. */
class Fixture {
public:
    Fixture() : store_(location()) {}

    [[nodiscard]] RouteStoreLocation location() const {
        return {.directory = directory(), .file = "route", .reports_queue = queue_.name()};
    }

    [[nodiscard]] std::string directory() const {
        return (directory_.path() / "store").string();
    }

    [[nodiscard]] fs::path file() const {
        return directory_.path() / "store" / "route";
    }

    /** Write `content` as the store's file, with `mode`, in its directory. */
    void plant(
            const std::string& content,
            fs::perms mode = fs::perms::owner_read | fs::perms::owner_write
    ) const {
        fs::create_directory(directory());
        fs::permissions(directory(), fs::perms::owner_all);
        std::ofstream{file()} << content;
        fs::permissions(file(), mode);
    }

    /** The line the store writes for the test queue's player. */
    [[nodiscard]] std::string line(const std::string& rest) const {
        const QueueIdentity player = queue_.identity();
        return "player=" + std::to_string(player.device) + ":" + std::to_string(player.inode) + " "
               + rest + "\n";
    }

    FileRouteStore& store() noexcept {
        return store_;
    }

    TestQueue& queue() noexcept {
        return queue_;
    }

private:
    testing::TemporaryDirectory directory_;
    TestQueue queue_;
    FileRouteStore store_;
};

/** Save `route` in a new fixture's store, and load it back. */
std::optional<StoredRoute> round_trip(const StoredRoute& route) {
    Fixture fixture;
    fixture.store().save(route);
    return fixture.store().load();
}

bool same(const std::optional<StoredRoute>& loaded, const StoredRoute& expected) {
    return loaded && loaded->applied == expected.applied && loaded->settled == expected.settled;
}

TEST_CASE("FileRouteStore keeps each route for the running player", "[platform][route_store]") {
    const Output speaker = output::Bluetooth{
            .device = output::BluetoothAddress::from("30:82:16:ab:38:a0").value()
    };
    const std::vector<StoredRoute> routes{
            {.applied = std::nullopt, .settled = false},
            {.applied = output::InternalDac{}, .settled = true},
            {.applied = output::Spdif{}, .settled = true},
            {.applied = output::UsbAudio{}, .settled = false},
            {.applied = speaker, .settled = true},
    };

    for (const StoredRoute& route : routes) {
        CHECK(same(round_trip(route), route));
    }
}

TEST_CASE("FileRouteStore creates its directory for its user alone", "[platform][route_store]") {
    Fixture fixture;
    const std::optional<StoredRoute> nothing_yet = fixture.store().load();
    fixture.store().save({.applied = output::Spdif{}, .settled = true});
    const fs::perms directory = fs::status(fixture.directory()).permissions();
    const fs::perms file = fs::status(fixture.file()).permissions();

    CHECK_FALSE(nothing_yet.has_value());
    CHECK(directory == fs::perms::owner_all);
    CHECK(file == (fs::perms::owner_read | fs::perms::owner_write));
}

TEST_CASE("FileRouteStore drops a route saved for another player", "[platform][route_store]") {
    Fixture fixture;
    fixture.store().save({.applied = output::Spdif{}, .settled = true});
    fixture.queue().recreate();
    const std::optional<StoredRoute> loaded = fixture.store().load();

    CHECK_FALSE(loaded.has_value());
    CHECK_FALSE(fs::exists(fixture.file()));
}

TEST_CASE("FileRouteStore takes a line written as it writes them", "[platform][route_store]") {
    Fixture fixture;
    fixture.plant(fixture.line("applied=bluetooth-30:82:16:AB:38:A0 settled=1"));
    const std::optional<StoredRoute> loaded = fixture.store().load();

    REQUIRE(loaded.has_value());
    CHECK(std::holds_alternative<output::Bluetooth>(loaded->applied.value()));
    CHECK(loaded->settled);
}

TEST_CASE("FileRouteStore ignores and removes a file it did not write", "[platform][route_store]") {
    const std::vector<std::string> malformed{
            "applied=headphones settled=1",
            "applied=bluetooth-30:82:16:ab:38:a0 settled=1",  // Not in its canonical form
            "applied=spdif settled=yes",
            "applied=spdif settled=1 extra=1",
    };
    for (const std::string& rest : malformed) {
        Fixture fixture;
        fixture.plant(fixture.line(rest));

        CHECK_FALSE(fixture.store().load().has_value());
        CHECK_FALSE(fs::exists(fixture.file()));
    }
    Fixture without_player;
    without_player.plant("applied=spdif settled=1\n");
    Fixture without_newline;
    std::string line = without_newline.line("applied=spdif settled=1");
    line.pop_back();
    without_newline.plant(line);
    Fixture readable_by_others;
    readable_by_others.plant(
            readable_by_others.line("applied=spdif settled=1"),
            fs::perms::owner_read | fs::perms::owner_write | fs::perms::others_read
    );

    CHECK_FALSE(without_player.store().load().has_value());
    CHECK_FALSE(without_newline.store().load().has_value());
    CHECK_FALSE(readable_by_others.store().load().has_value());
    CHECK_FALSE(fs::exists(readable_by_others.file()));
}

TEST_CASE("FileRouteStore does not follow a link in its file's place", "[platform][route_store]") {
    Fixture fixture;
    fixture.plant("");
    const fs::path target = fixture.file().string() + ".target";
    fs::rename(fixture.file(), target);
    fs::create_symlink(target, fixture.file());
    const std::optional<StoredRoute> loaded = fixture.store().load();

    CHECK_FALSE(loaded.has_value());
    CHECK_FALSE(fs::is_symlink(fs::symlink_status(fixture.file())));
    CHECK(fs::exists(target));
}

TEST_CASE(
        "FileRouteStore leaves a directory that is not its own alone", "[platform][route_store]"
) {
    Fixture fixture;
    fixture.plant(fixture.line("applied=spdif settled=1"));
    fs::permissions(fixture.directory(), fs::perms::owner_all | fs::perms::group_read);
    const std::optional<StoredRoute> loaded = fixture.store().load();
    fixture.store().save({.applied = output::UsbAudio{}, .settled = true});
    std::ifstream file{fixture.file()};
    std::string kept;
    std::getline(file, kept);

    CHECK_FALSE(loaded.has_value());
    CHECK(kept + "\n" == fixture.line("applied=spdif settled=1"));
}

TEST_CASE("FileRouteStore saves nothing while no player runs", "[platform][route_store]") {
    testing::TemporaryDirectory directory;
    FileRouteStore store{{
            .directory = (directory.path() / "store").string(),
            .file = "route",
            .reports_queue = "/diskos-test-no-such-queue",
    }};
    store.save({.applied = output::Spdif{}, .settled = true});

    CHECK_FALSE(store.load().has_value());
    CHECK_FALSE(fs::exists(directory.path() / "store" / "route"));
}

}  // namespace
}  // namespace diskos::platform

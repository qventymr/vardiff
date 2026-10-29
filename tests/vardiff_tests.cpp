// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

#include <vardiff/vardiff.hpp>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            throw std::runtime_error(std::string(__FILE__) + ":" +              \
                std::to_string(__LINE__) + ": " #condition);                    \
        }                                                                       \
    } while (false)

bool header_smoke();

namespace {

struct Player {
    int hp = 100;
    int coins = 0;
};

struct AllTypes {
    int count = 1;
    float speed = 1.25F;
    double distance = 2.5;
    bool active = false;
    std::string name = "before";
};

template <typename T>
concept can_make_field = requires(T AllTypes::* member) {
    vardiff::field("test", member);
};

template <typename T>
concept can_track_player = requires(T&& object) {
    vardiff::track(std::forward<T>(object), vardiff::field("hp", &Player::hp));
};

template <typename T>
concept can_track_without_fields = requires(T& object) {
    vardiff::track(object);
};

static_assert(can_make_field<int>);
static_assert(can_make_field<float>);
static_assert(can_make_field<double>);
static_assert(can_make_field<bool>);
static_assert(can_make_field<std::string>);
static_assert(can_make_field<long>);
static_assert(can_make_field<std::vector<int>>);
static_assert(!can_make_field<volatile int>);
static_assert(can_track_player<Player&>);
static_assert(can_track_player<const Player&>);
static_assert(!can_track_player<Player>);
static_assert(!can_track_player<const Player>);
static_assert(!can_track_player<AllTypes&>);
static_assert(!can_track_without_fields<Player>);

void initial_state_and_untracked_fields() {
    Player player;
    const auto watcher = vardiff::track(player, vardiff::field("hp", &Player::hp));
    CHECK(!watcher.changed());
    CHECK(watcher.diff().empty());
    player.coins = 42;
    CHECK(!watcher.changed());
    CHECK(watcher.diff().empty());
}

void repeated_reads_keep_baseline() {
    Player player;
    const auto watcher = vardiff::track(
        player, vardiff::field("hp", &Player::hp),
        vardiff::field("coins", &Player::coins));
    player.hp = 75;
    CHECK(watcher.changed());
    CHECK(watcher.changed());
    for (int i = 0; i < 2; ++i) {
        const auto changes = watcher.diff();
        CHECK(changes.size() == 1);
        CHECK(changes[0].name == "hp");
        CHECK(changes[0].before.get<int>() == 100);
        CHECK(changes[0].after.get<int>() == 75);
    }
    player.hp = 50;
    CHECK(watcher.diff()[0].before.get<int>() == 100);
    CHECK(watcher.diff()[0].after.get<int>() == 50);
}

void returning_to_baseline_removes_change() {
    Player player;
    auto watcher = vardiff::track(player, vardiff::field("hp", &Player::hp));
    player.hp = 0;
    CHECK(watcher.changed());
    player.hp = 100;
    CHECK(!watcher.changed());
    CHECK(watcher.diff().empty());
}

void snapshot_replaces_baseline() {
    Player player;
    auto watcher = vardiff::track(
        player, vardiff::field("hp", &Player::hp),
        vardiff::field("coins", &Player::coins));
    player.hp = 75;
    player.coins = 10;
    watcher.snapshot();
    CHECK(!watcher.changed());
    CHECK(watcher.diff().empty());
    player.coins = 20;
    const auto changes = watcher.diff();
    CHECK(changes.size() == 1);
    CHECK(changes[0].name == "coins");
    CHECK(changes[0].before.get<int>() == 10);
    CHECK(changes[0].after.get<int>() == 20);
    watcher.snapshot();
    watcher.snapshot();
    CHECK(!watcher.changed());
}

void all_types_and_descriptor_order() {
    AllTypes object;
    auto watcher = vardiff::track(
        object, vardiff::field("name", &AllTypes::name),
        vardiff::field("active", &AllTypes::active),
        vardiff::field("distance", &AllTypes::distance),
        vardiff::field("speed", &AllTypes::speed),
        vardiff::field("count", &AllTypes::count));
    object = AllTypes{-4, 3.5F, 4.75, true, "after"};
    CHECK(watcher.changed());
    const auto changes = watcher.diff();
    CHECK(changes.size() == 5);
    CHECK(changes[0].name == "name");
    CHECK(changes[0].before.get<std::string>() == "before");
    CHECK(changes[0].after.get<std::string>() == "after");
    CHECK(changes[1].name == "active");
    CHECK(!changes[1].before.get<bool>());
    CHECK(changes[1].after.get<bool>());
    CHECK(changes[2].name == "distance");
    CHECK(changes[2].before.get<double>() == 2.5);
    CHECK(changes[2].after.get<double>() == 4.75);
    CHECK(changes[3].name == "speed");
    CHECK(changes[3].before.get<float>() == 1.25F);
    CHECK(changes[3].after.get<float>() == 3.5F);
    CHECK(changes[4].name == "count");
    CHECK(changes[4].before.get<int>() == 1);
    CHECK(changes[4].after.get<int>() == -4);
    watcher.snapshot();
    CHECK(!watcher.changed());
    CHECK(watcher.diff().empty());
}

void each_type_is_detected_independently() {
    AllTypes object;
    auto watcher = vardiff::track(
        object, vardiff::field("count", &AllTypes::count),
        vardiff::field("speed", &AllTypes::speed),
        vardiff::field("distance", &AllTypes::distance),
        vardiff::field("active", &AllTypes::active),
        vardiff::field("name", &AllTypes::name));
    auto check_single = [&](const std::string& name) {
        CHECK(watcher.changed());
        const auto changes = watcher.diff();
        CHECK(changes.size() == 1);
        CHECK(changes[0].name == name);
        watcher.snapshot();
        CHECK(!watcher.changed());
    };
    object.count = 2;
    check_single("count");
    object.speed = std::nextafter(object.speed, 2.0F);
    check_single("speed");
    object.distance = std::nextafter(object.distance, 3.0);
    check_single("distance");
    object.active = true;
    check_single("active");
    object.name.clear();
    check_single("name");
}

void string_snapshots_and_names_are_owned() {
    AllTypes object;
    object.name.assign(256, 'a');
    std::string label = "name";
    auto descriptor = vardiff::field(label, &AllTypes::name);
    label.assign(100, 'x');
    auto watcher = vardiff::track(object, descriptor);
    descriptor.name = "renamed";
    object.name[128] = 'b';
    const auto changes = watcher.diff();
    CHECK(changes[0].name == "name");
    CHECK(changes[0].before.get<std::string>() == std::string(256, 'a'));
    CHECK(changes[0].after.get<std::string>()[128] == 'b');
}

void returned_diff_outlives_source() {
    std::vector<vardiff::change> changes;
    {
        AllTypes object;
        auto watcher = vardiff::track(object, vardiff::field("name", &AllTypes::name));
        object.name = "after";
        changes = watcher.diff();
        object.name = "later";
        watcher.snapshot();
    }
    CHECK(changes[0].name == "name");
    CHECK(changes[0].before.get<std::string>() == "before");
    CHECK(changes[0].after.get<std::string>() == "after");
}

void const_objects_and_const_members() {
    const Player player;
    auto watcher = vardiff::track(player, vardiff::field("hp", &Player::hp));
    CHECK(!watcher.changed());
    watcher.snapshot();
    CHECK(watcher.diff().empty());

    struct Fixed { const int id = 7; } fixed;
    auto fixed_watcher = vardiff::track(fixed, vardiff::field("id", &Fixed::id));
    CHECK(!fixed_watcher.changed());
    fixed_watcher.snapshot();

    Player mutable_player;
    const Player& view = mutable_player;
    const auto view_watcher = vardiff::track(view, vardiff::field("hp", &Player::hp));
    mutable_player.hp = 10;
    CHECK(view_watcher.changed());
    CHECK(view_watcher.diff()[0].after.get<int>() == 10);
}

void watcher_copies_have_independent_snapshots() {
    Player player;
    auto first = vardiff::track(player, vardiff::field("hp", &Player::hp));
    auto second = first;
    player.hp = 75;
    first.snapshot();
    CHECK(!first.changed());
    CHECK(second.changed());
    CHECK(second.diff()[0].before.get<int>() == 100);
    auto moved = std::move(second);
    CHECK(moved.changed());
    CHECK(moved.diff()[0].after.get<int>() == 75);
}

void exact_floating_point_equality() {
    AllTypes object;
    object.speed = 0.0F;
    object.distance = std::numeric_limits<double>::infinity();
    auto watcher = vardiff::track(
        object, vardiff::field("speed", &AllTypes::speed),
        vardiff::field("distance", &AllTypes::distance));
    object.speed = -0.0F;
    CHECK(!watcher.changed());
    object.distance = std::numeric_limits<double>::quiet_NaN();
    CHECK(watcher.changed());
    CHECK(watcher.diff().size() == 1);
    watcher.snapshot();
    // NaN == NaN is false, including immediately after snapshot().
    CHECK(watcher.changed());
    CHECK(std::isnan(watcher.diff()[0].before.get<double>()));
    CHECK(std::isnan(watcher.diff()[0].after.get<double>()));
}

void streaming_values() {
    AllTypes object;
    auto watcher = vardiff::track(
        object, vardiff::field("count", &AllTypes::count),
        vardiff::field("speed", &AllTypes::speed),
        vardiff::field("distance", &AllTypes::distance),
        vardiff::field("active", &AllTypes::active),
        vardiff::field("name", &AllTypes::name));
    object = AllTypes{2, 3.5F, 4.75, true, "after"};
    std::ostringstream output;
    output << std::boolalpha;
    for (const auto& change : watcher.diff()) {
        output << change.name << ": " << change.before << " -> " << change.after << '\n';
    }
    CHECK(output.str() == "count: 1 -> 2\nspeed: 1.25 -> 3.5\n"
                          "distance: 2.5 -> 4.75\nactive: false -> true\n"
                          "name: before -> after\n");
    std::ostringstream formatted;
    formatted << std::fixed << std::setprecision(3) << vardiff::value(1.25);
    CHECK(formatted.str() == "1.250");
}

void wrong_value_type_throws() {
    const vardiff::value value(42);
    bool threw = false;
    try {
        (void)value.get<double>();
    } catch (const std::bad_variant_access&) {
        threw = true;
    }
    CHECK(threw);
}

void null_member_pointer_is_rejected() {
    int Player::* member = nullptr;
    bool threw = false;
    try {
        (void)vardiff::field("hp", member);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"initial state and untracked fields", initial_state_and_untracked_fields},
        {"repeated reads keep baseline", repeated_reads_keep_baseline},
        {"reverted changes", returning_to_baseline_removes_change},
        {"snapshot replaces baseline", snapshot_replaces_baseline},
        {"all types and descriptor order", all_types_and_descriptor_order},
        {"independent changes for every type", each_type_is_detected_independently},
        {"owned string snapshots and names", string_snapshots_and_names_are_owned},
        {"owned diff lifetime", returned_diff_outlives_source},
        {"const objects and members", const_objects_and_const_members},
        {"independent watcher copies", watcher_copies_have_independent_snapshots},
        {"floating point equality", exact_floating_point_equality},
        {"streaming values", streaming_values},
        {"wrong value type", wrong_value_type_throws},
        {"null member pointer", null_member_pointer_is_rejected},
        {"multiple translation units", [] { CHECK(header_smoke()); }},
    };
    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            std::cout << "PASS: " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << test.name << ": " << error.what() << '\n';
        }
    }
    std::cout << std::size(tests) - failures << "/" << std::size(tests)
              << " tests passed\n";
    return failures == 0 ? 0 : 1;
}

// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

#include <vardiff/vardiff.hpp>

#include <cmath>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            throw std::runtime_error(std::string(__FILE__) + ":" +              \
                std::to_string(__LINE__) + ": " #condition);                    \
        }                                                                       \
    } while (false)

struct Profile {
    int score = 10;
    std::string label = "first";
    int transient = 0; // Deliberately unregistered; no operator==.
};

template <>
struct vardiff::schema<Profile> {
    static auto fields() {
        return std::make_tuple(vardiff::field("score", &Profile::score),
                               vardiff::field("label", &Profile::label));
    }
};

struct Document {
    int id = 1;
    Profile profile;
    std::vector<Profile> items{{}, {}};
    std::optional<Profile> selected;
    std::array<int, 3> levels{1, 2, 3};
    std::vector<std::optional<std::array<int, 2>>> nested{{std::array{1, 2}}};
    std::vector<bool> flags{false, true};
};

template <>
struct vardiff::schema<Document> {
    static auto fields() {
        return std::make_tuple(
            vardiff::field("id", &Document::id),
            vardiff::field("profile", &Document::profile),
            vardiff::field("items", &Document::items),
            vardiff::field("selected", &Document::selected),
            vardiff::field("levels", &Document::levels),
            vardiff::field("nested", &Document::nested),
            vardiff::field("flags", &Document::flags));
    }
};

struct Node {
    int number = 0;
    std::vector<Node> children;
};

template <>
struct vardiff::schema<Node> {
    static auto fields() {
        return std::make_tuple(vardiff::field("number", &Node::number),
                               vardiff::field("children", &Node::children));
    }
};

namespace {

struct Player { int hp = 100; int coins = 0; int ignored = 0; };
struct Opaque { int data; };
struct NoCopy {
    NoCopy() = default;
    NoCopy(const NoCopy&) = delete;
    bool operator==(const NoCopy&) const = default;
};
struct CopyOnly {
    int data = 1;
    CopyOnly() = default;
    CopyOnly(const CopyOnly&) = default;
    CopyOnly(CopyOnly&&) = delete;
    CopyOnly& operator=(const CopyOnly&) = default;
    bool operator==(const CopyOnly&) const = default;
};
struct NonDefault {
    int data;
    explicit NonDefault(int input) : data(input) {}
    bool operator==(const NonDefault&) const = default;
};

struct Counted {
    static inline int copies = 0;
    static inline int comparisons = 0;
    static inline bool throw_copy = false;
    int data = 0;
    Counted() = default;
    explicit Counted(int input) : data(input) {}
    Counted(const Counted& other) : data(other.data) {
        if (throw_copy) {
            throw std::runtime_error("copy failed");
        }
        ++copies;
    }
    Counted(Counted&&) = default;
    Counted& operator=(const Counted&) = default;
    bool operator==(const Counted& other) const {
        ++comparisons;
        return data == other.data;
    }
};

template <typename T>
concept has_undo = requires(T& watcher) { watcher.undo(); };

template <typename T>
concept can_track_registered = requires(T&& object) {
    vardiff::track(std::forward<T>(object));
};

using MutableWatcher = decltype(vardiff::track(
    std::declval<Player&>(), vardiff::field("hp", &Player::hp)));
using ConstWatcher = decltype(vardiff::track(
    std::declval<const Player&>(), vardiff::field("hp", &Player::hp)));
static_assert(has_undo<MutableWatcher>);
static_assert(!has_undo<ConstWatcher>);
static_assert(vardiff::supported_type<Profile>);
static_assert(vardiff::supported_type<std::vector<Profile>>);
static_assert(vardiff::supported_type<CopyOnly>);
static_assert(!vardiff::supported_type<NoCopy>);
static_assert(!vardiff::supported_type<std::unique_ptr<int>>);
static_assert(!vardiff::supported_type<Opaque>);
static_assert(can_track_registered<Document&>);
static_assert(can_track_registered<const Document&>);
static_assert(!can_track_registered<Document>);
static_assert(!can_track_registered<const Document>);

auto track_player(Player& player, vardiff::watch_options options = {}) {
    return vardiff::track(player, options, vardiff::field("hp", &Player::hp),
                          vardiff::field("coins", &Player::coins));
}

template <typename Exception, typename Function>
void expect_throw(Function&& function) {
    bool threw = false;
    try {
        function();
    } catch (const Exception&) {
        threw = true;
    }
    CHECK(threw);
}

void callbacks_are_deferred() {
    Player player;
    auto watcher = track_player(player);
    int calls = 0;
    watcher.on_change([&](const auto& changes) {
        ++calls;
        CHECK(changes.size() == 1);
        CHECK(changes[0].name == "hp");
    });
    player.hp = 75;
    CHECK(calls == 0);
    CHECK(watcher.changed());
    CHECK(watcher.diff().size() == 1);
    CHECK(calls == 0);
    const auto changes = watcher.poll();
    CHECK(calls == 1);
    CHECK(changes[0].before.get<int>() == 100);
    CHECK(changes[0].after.get<int>() == 75);
    CHECK(!watcher.changed());
    CHECK(watcher.history().size() == 2);
    CHECK(watcher.poll().empty());
    CHECK(calls == 1);
    watcher.snapshot();
    CHECK(watcher.undo());
    CHECK(watcher.redo());
    CHECK(calls == 1);
}

void callback_order_and_path_filters() {
    Document object;
    auto watcher = vardiff::track(object);
    std::vector<std::string> calls;
    watcher.on_change([&](const auto&) { calls.push_back("batch"); });
    watcher.on_change("profile", [&](const auto& change) { calls.push_back(change.name); });
    watcher.on_change("pro", [&](const auto&) { calls.push_back("wrong-prefix"); });
    watcher.on_change("items[1]", [&](const auto& change) { calls.push_back(change.name); });
    object.profile.score = 20;
    object.profile.label = "new";
    object.items[1].score = 30;
    watcher.poll();
    CHECK((calls == std::vector<std::string>{
        "batch", "profile.score", "profile.label", "items[1].score"}));
}

void auto_snapshot_can_be_disabled() {
    Player player;
    auto watcher = track_player(player, {.auto_snapshot = false, .history_limit = 4});
    int calls = 0;
    watcher.on_change([&](const auto&) { ++calls; });
    player.hp = 75;
    watcher.poll();
    watcher.poll();
    CHECK(calls == 2);
    CHECK(watcher.changed());
    CHECK(watcher.history().size() == 1);
    watcher.snapshot();
    CHECK(watcher.poll().empty());
    watcher.set_auto_snapshot(true);
    CHECK(watcher.auto_snapshot_enabled());
    player.hp = 50;
    watcher.poll();
    CHECK(calls == 3);
    CHECK(!watcher.changed());
}

void stateful_callbacks_keep_their_state() {
    Player player;
    auto watcher = track_player(player);
    std::vector<int> seen;
    watcher.on_change([counter = 0, &seen](const auto&) mutable { seen.push_back(++counter); });
    player.hp = 75;
    watcher.poll();
    player.hp = 50;
    watcher.poll();
    CHECK((seen == std::vector<int>{1, 2}));
}

void callback_mutations_remain_pending() {
    Player player;
    auto watcher = track_player(player);
    bool first = true;
    watcher.on_change([&](const auto&) {
        if (first) {
            first = false;
            player.hp = 25;
        }
    });
    player.hp = 75;
    const auto observed = watcher.poll();
    CHECK(observed[0].after.get<int>() == 75);
    CHECK(player.hp == 25);
    CHECK(watcher.changed());
    CHECK(watcher.diff()[0].before.get<int>() == 75);
    CHECK(watcher.poll()[0].after.get<int>() == 25);
    CHECK(!watcher.changed());
}

void registrations_during_dispatch_are_deferred() {
    Player player;
    auto watcher = track_player(player);
    bool registered = false;
    int late_calls = 0;
    watcher.on_change([&](const auto&) {
        if (!registered) {
            registered = true;
            for (int i = 0; i < 256; ++i) {
                watcher.on_change([&](const auto&) { ++late_calls; });
            }
        }
    });
    player.hp = 75;
    watcher.poll();
    CHECK(late_calls == 0);
    player.hp = 50;
    watcher.poll();
    CHECK(late_calls == 256);
}

void callback_exception_keeps_baseline() {
    Player player;
    auto watcher = track_player(player);
    bool fail = true;
    int later_calls = 0;
    watcher.on_change([&](const auto&) {
        if (std::exchange(fail, false)) {
            throw std::runtime_error("callback failed");
        }
    });
    watcher.on_change([&](const auto&) { ++later_calls; });
    player.hp = 75;
    expect_throw<std::runtime_error>([&] { watcher.poll(); });
    CHECK(later_calls == 0);
    CHECK(watcher.history().size() == 1);
    CHECK(watcher.diff()[0].before.get<int>() == 100);
    watcher.poll();
    CHECK(later_calls == 1);
    CHECK(!watcher.changed());
}

void recursive_poll_and_history_mutation_are_rejected() {
    Player player;
    auto watcher = track_player(player);
    watcher.on_change([&](const auto&) {
        CHECK(watcher.changed());
        CHECK(watcher.diff().size() == 1);
        expect_throw<std::logic_error>([&] { watcher.poll(); });
        expect_throw<std::logic_error>([&] { watcher.snapshot(); });
        expect_throw<std::logic_error>([&] { watcher.undo(); });
        expect_throw<std::logic_error>([&] { watcher.redo(); });
        expect_throw<std::logic_error>([&] { watcher.rollback(); });
        expect_throw<std::logic_error>([&] { watcher.rollback(0); });
        expect_throw<std::logic_error>([&] { watcher.set_history_limit(2); });
        expect_throw<std::logic_error>([&] { auto copy = watcher; (void)copy; });
        expect_throw<std::logic_error>([&] { auto moved = std::move(watcher); (void)moved; });
        expect_throw<std::logic_error>([&] { watcher = track_player(player); });
    });
    player.hp = 75;
    watcher.poll();
    CHECK(!watcher.changed());
}

void ignore_top_level_and_prefix_boundaries() {
    Player player;
    auto watcher = track_player(player);
    watcher.ignore("hp").ignore("hp").ignore("coin");
    player.hp = 75;
    CHECK(!watcher.changed());
    CHECK(watcher.diff().empty());
    player.coins = 5;
    CHECK(watcher.changed());
    const auto changes = watcher.poll();
    CHECK(changes.size() == 1);
    CHECK(changes[0].name == "coins");
    CHECK(watcher.undo());
    CHECK(player.coins == 0);
    CHECK(player.hp == 75);
}

void ignore_nested_fields_and_elements() {
    Document object;
    auto watcher = vardiff::track(object);
    watcher.ignore("profile.label").ignore("items[1]").ignore("levels[2]");
    object.profile.label = "ignored";
    object.items[1].score = 99;
    object.levels[2] = 100;
    CHECK(!watcher.changed());
    object.profile.score = 20;
    const auto changes = watcher.poll();
    CHECK(changes.size() == 1);
    CHECK(changes[0].name == "profile.score");
    CHECK(watcher.undo());
    CHECK(object.profile.score == 10);
    CHECK(object.profile.label == "ignored");
    CHECK(object.items[1].score == 99);
    CHECK(object.levels[2] == 100);
}

void custom_comparison_uses_original_baseline() {
    struct Temperature { double degrees = 10.0; } object;
    auto watcher = vardiff::track(object, vardiff::field(
        "degrees", &Temperature::degrees,
        [](double a, double b) { return std::abs(a - b) < 0.1; }));
    object.degrees = 10.05;
    CHECK(!watcher.changed());
    CHECK(watcher.poll().empty());
    CHECK(watcher.history().size() == 1);
    object.degrees = 10.15;
    CHECK(watcher.changed());
    const auto changes = watcher.poll();
    CHECK(changes[0].before.get<double>() == 10.0);
    CHECK(changes[0].after.get<double>() == 10.15);
}

void comparator_only_custom_type() {
    struct Object { Opaque opaque{1}; } object;
    auto watcher = vardiff::track(object, vardiff::field(
        "opaque", &Object::opaque,
        [](const Opaque& a, const Opaque& b) { return a.data == b.data; }));
    object.opaque.data = 2;
    const auto changes = watcher.poll();
    CHECK(changes[0].before.get<Opaque>().data == 1);
    CHECK(changes[0].after.get<Opaque>().data == 2);
    CHECK(watcher.undo());
    CHECK(object.opaque.data == 1);
}

void custom_comparator_is_atomic() {
    Document object;
    auto watcher = vardiff::track(object, vardiff::field(
        "profile", &Document::profile,
        [](const Profile& a, const Profile& b) { return a.score == b.score; }));
    object.profile.label = "uncompared";
    CHECK(!watcher.changed());
    object.profile.score = 20;
    const auto changes = watcher.poll();
    CHECK(changes.size() == 1);
    CHECK(changes[0].name == "profile");
    CHECK(changes[0].after.get<Profile>().label == "uncompared");
    CHECK(watcher.undo());
    CHECK(object.profile.label == "first");
}

void history_returns_independent_snapshots() {
    Player player;
    auto watcher = track_player(player);
    auto initial = watcher.history();
    CHECK(initial.size() == 1);
    CHECK(std::get<0>(initial[0]) == 100);
    CHECK(std::get<1>(initial[0]) == 0);
    std::get<0>(initial[0]) = -1;
    player.hp = 75;
    watcher.snapshot();
    player.hp = 50;
    watcher.snapshot();
    const auto history = watcher.history();
    CHECK(history.size() == 3);
    CHECK(std::get<0>(history[0]) == 100);
    CHECK(std::get<0>(history[1]) == 75);
    CHECK(std::get<0>(history[2]) == 50);
    CHECK(watcher.history_position() == 2);
}

void undo_redo_and_rollback() {
    Player player;
    auto watcher = track_player(player);
    CHECK(!watcher.undo());
    CHECK(!watcher.redo());
    player.hp = 75;
    watcher.snapshot();
    player.hp = 50;
    player.coins = 10;
    watcher.snapshot();
    player.ignored = 99;
    CHECK(watcher.undo());
    CHECK(player.hp == 75);
    CHECK(player.coins == 0);
    CHECK(player.ignored == 99);
    CHECK(!watcher.changed());
    CHECK(watcher.rollback());
    CHECK(player.hp == 100);
    CHECK(!watcher.undo());
    CHECK(watcher.redo());
    CHECK(player.hp == 75);
    CHECK(watcher.redo());
    CHECK(player.hp == 50);
    CHECK(player.coins == 10);
    CHECK(!watcher.redo());
}

void unsaved_edits_are_not_committed_by_undo() {
    Player player;
    auto watcher = track_player(player);
    player.hp = 75;
    watcher.snapshot();
    player.hp = 25;
    CHECK(watcher.undo());
    CHECK(player.hp == 100);
    CHECK(watcher.history().size() == 2);
    CHECK(watcher.redo());
    CHECK(player.hp == 75);
}

void rollback_to_a_selected_snapshot() {
    Player player;
    auto watcher = track_player(player);
    player.hp = 75;
    watcher.snapshot();
    player.hp = 50;
    watcher.snapshot();
    CHECK(watcher.history_size() == 3);
    CHECK(watcher.rollback(0));
    CHECK(player.hp == 100);
    CHECK(watcher.can_redo());
    CHECK(watcher.rollback(2));
    CHECK(player.hp == 50);
    player.hp = 1;
    CHECK(watcher.rollback(watcher.history_position()));
    CHECK(player.hp == 50);
    expect_throw<std::out_of_range>([&] { watcher.rollback(3); });
    CHECK(watcher.history_position() == 2);
}

void new_snapshot_or_poll_discards_redo() {
    Player player;
    auto watcher = track_player(player);
    player.hp = 75;
    watcher.snapshot();
    player.hp = 50;
    watcher.snapshot();
    CHECK(watcher.undo());
    player.hp = 60;
    watcher.snapshot();
    CHECK(!watcher.can_redo());
    CHECK(watcher.history().size() == 3);
    CHECK(watcher.undo());
    player.hp = 65;
    watcher.poll();
    CHECK(!watcher.redo());
    CHECK(std::get<0>(watcher.history().back()) == 65);
}

void bounded_history_and_limit_changes() {
    Player player;
    auto watcher = track_player(player, {.history_limit = 3});
    for (int i = 1; i <= 5; ++i) {
        player.hp = i;
        watcher.snapshot();
    }
    auto history = watcher.history();
    CHECK(history.size() == 3);
    CHECK(std::get<0>(history.front()) == 3);
    CHECK(watcher.undo());
    CHECK(player.hp == 4);
    watcher.set_history_limit(1);
    CHECK(watcher.history_limit() == 1);
    CHECK(watcher.history().size() == 1);
    CHECK(watcher.history_position() == 0);
    CHECK(!watcher.can_undo());
    CHECK(!watcher.can_redo());
    CHECK(!watcher.changed());
    player.hp = 9;
    watcher.poll();
    CHECK(watcher.history().size() == 1);
    CHECK(!watcher.changed());
    expect_throw<std::invalid_argument>([&] { watcher.set_history_limit(0); });
    CHECK(watcher.history_limit() == 1);
    expect_throw<std::invalid_argument>([&] {
        auto invalid = track_player(player, {.history_limit = 0});
        (void)invalid;
    });
}

void independent_objects_and_registered_fields() {
    const Document before;
    auto after = before;
    after.profile.score = 20;
    after.profile.transient = 99;
    after.items[1].label = "changed";
    CHECK(vardiff::changed(before, after));
    const auto changes = vardiff::diff(before, after);
    CHECK(changes.size() == 2);
    CHECK(changes[0].name == "profile.score");
    CHECK(changes[0].before.get<int>() == 10);
    CHECK(changes[1].name == "items[1].label");
    CHECK(before.profile.score == 10);
    CHECK(after.profile.score == 20);
    CHECK(vardiff::diff(before, after, vardiff::field("id", &Document::id)).empty());
    CHECK(!vardiff::changed(before, after, vardiff::field("id", &Document::id)));
    after = before;
    after.profile.transient = 100;
    CHECK(!vardiff::changed(before, after));
    CHECK(vardiff::diff(before, after).empty());
}

void vectors_add_remove_and_modify_by_index() {
    struct Object { std::vector<int> items{1, 2}; } object;
    auto watcher = vardiff::track(object, vardiff::field("items", &Object::items));
    object.items = {3, 2, 4};
    const auto changes = watcher.poll();
    CHECK(changes.size() == 2);
    CHECK(changes[0].name == "items[0]");
    CHECK(changes[0].kind == vardiff::change_kind::modified);
    CHECK(changes[1].name == "items[2]");
    CHECK(changes[1].kind == vardiff::change_kind::added);
    CHECK(!changes[1].before.has_value());
    CHECK(changes[1].after.get<int>() == 4);
    object.items = {};
    const auto removed = watcher.poll();
    CHECK(removed.size() == 3);
    CHECK(removed[2].kind == vardiff::change_kind::removed);
    CHECK(removed[2].before.get<int>() == 4);
    CHECK(!removed[2].after.has_value());
    CHECK(watcher.undo());
    CHECK((object.items == std::vector<int>{3, 2, 4}));
    CHECK(watcher.undo());
    CHECK((object.items == std::vector<int>{1, 2}));
    CHECK(watcher.redo());
    CHECK((object.items == std::vector<int>{3, 2, 4}));
}

void added_registered_container_items_are_owned() {
    Document object;
    auto watcher = vardiff::track(object);
    object.items.push_back(Profile{30, "third", 7});
    const auto changes = watcher.poll();
    CHECK(changes.size() == 1);
    CHECK(changes[0].name == "items[2]");
    CHECK(changes[0].after.get<Profile>().score == 30);
    CHECK(changes[0].after.get<Profile>().label == "third");
    object.items.clear();
    CHECK(changes[0].after.get<Profile>().label == "third");
}

void optionals_presence_and_nested_fields() {
    Document object;
    auto watcher = vardiff::track(object);
    object.selected = Profile{20, "selected", 0};
    auto changes = watcher.poll();
    CHECK(changes.size() == 1);
    CHECK(changes[0].name == "selected");
    CHECK(changes[0].kind == vardiff::change_kind::added);
    CHECK(changes[0].after.get<Profile>().score == 20);
    object.selected->score = 30;
    changes = watcher.poll();
    CHECK(changes[0].name == "selected.score");
    CHECK(changes[0].before.get<int>() == 20);
    object.selected.reset();
    changes = watcher.poll();
    CHECK(changes[0].name == "selected");
    CHECK(changes[0].kind == vardiff::change_kind::removed);
    CHECK(changes[0].before.get<Profile>().score == 30);
    CHECK(watcher.undo());
    CHECK(object.selected->score == 30);
    CHECK(watcher.undo());
    CHECK(object.selected->score == 20);
    CHECK(watcher.undo());
    CHECK(!object.selected);
    CHECK(watcher.redo());
    CHECK(object.selected->label == "selected");
}

void arrays_and_deep_container_paths() {
    Document object;
    auto watcher = vardiff::track(object);
    object.levels[1] = 20;
    (*object.nested[0])[1] = 30;
    object.nested.push_back(std::nullopt);
    const auto changes = watcher.poll();
    CHECK(changes.size() == 3);
    CHECK(changes[0].name == "levels[1]");
    CHECK(changes[1].name == "nested[0][1]");
    CHECK(changes[1].after.get<int>() == 30);
    CHECK(changes[2].name == "nested[1]");
    CHECK(changes[2].after.has_value()); // Present vector item holding an empty optional.
    CHECK((!changes[2].after.get<std::optional<std::array<int, 2>>>()));
    CHECK(watcher.undo());
    CHECK(object.levels[1] == 2);
    CHECK(object.nested.size() == 1);
    CHECK((*object.nested[0])[1] == 2);
}

void recursive_value_trees() {
    Node root{1, {Node{2, {Node{3, {}}}}}};
    const Node before = root;
    auto watcher = vardiff::track(root);
    root.children[0].children[0].number = 4;
    const auto changes = watcher.poll();
    CHECK(changes.size() == 1);
    CHECK(changes[0].name == "children[0].children[0].number");
    CHECK(changes[0].before.get<int>() == 3);
    CHECK(changes[0].after.get<int>() == 4);
    CHECK(vardiff::diff(before, root)[0].name == changes[0].name);
    CHECK(watcher.undo());
    CHECK(root.children[0].children[0].number == 3);
    CHECK(watcher.redo());
    CHECK(root.children[0].children[0].number == 4);
}

void vector_bool_and_empty_array() {
    struct Object { std::vector<bool> bits{false, true}; std::array<int, 0> empty; } object;
    auto watcher = vardiff::track(object, vardiff::field("bits", &Object::bits),
                                  vardiff::field("empty", &Object::empty));
    object.bits[0] = true;
    object.bits.push_back(false);
    const auto changes = watcher.poll();
    CHECK(changes.size() == 2);
    CHECK(!changes[0].before.get<bool>());
    CHECK(changes[0].after.get<bool>());
    CHECK(!changes[1].after.get<bool>());
    CHECK(watcher.undo());
    CHECK((object.bits == std::vector<bool>{false, true}));
    CHECK(!watcher.changed());
}

void containers_of_nondefault_constructible_elements() {
    struct Object {
        std::vector<NonDefault> items{NonDefault{1}, NonDefault{2}};
        std::optional<NonDefault> choice{NonDefault{3}};
    } object;
    auto watcher = vardiff::track(object, vardiff::field("items", &Object::items),
                                  vardiff::field("choice", &Object::choice));
    object.items.clear();
    object.choice.reset();
    watcher.snapshot();
    CHECK(watcher.undo());
    CHECK(object.items.size() == 2);
    CHECK(object.items[1].data == 2);
    CHECK(object.choice->data == 3);
    CHECK(watcher.redo());
    CHECK(object.items.empty());
    CHECK(!object.choice);
}

void restoring_registered_fields_preserves_unregistered_data() {
    Document object;
    auto watcher = vardiff::track(object);
    object.profile.score = 20;
    object.profile.transient = 50;
    object.items[0].score = 30;
    watcher.snapshot();
    object.profile.transient = 99;
    object.items[0].transient = 88;
    CHECK(watcher.undo());
    CHECK(object.profile.score == 10);
    CHECK(object.profile.transient == 99);
    CHECK(object.items[0].score == 10);
    CHECK(object.items[0].transient == 88);
}

void read_only_objects_can_poll_and_keep_history() {
    Player player;
    const Player& view = player;
    auto watcher = vardiff::track(view, vardiff::field("hp", &Player::hp));
    player.hp = 75;
    CHECK(watcher.poll().size() == 1);
    CHECK(!watcher.changed());
    CHECK(watcher.history().size() == 2);
}

void idle_checks_do_not_copy_and_changed_short_circuits() {
    struct Object { Counted first{1}; Counted second{2}; } object;
    auto watcher = vardiff::track(object, vardiff::field("first", &Object::first),
                                  vardiff::field("second", &Object::second));
    Counted::copies = 0;
    CHECK(!watcher.changed());
    CHECK(watcher.diff().empty());
    CHECK(watcher.poll().empty());
    CHECK(Counted::copies == 0);
    object.first.data = 10;
    Counted::comparisons = 0;
    CHECK(watcher.changed());
    CHECK(Counted::comparisons == 1);
    CHECK(Counted::copies == 0);
    watcher.snapshot();
    CHECK(Counted::copies == 2); // Exactly one copy per selected field.
}

void snapshot_exception_preserves_history_and_redo() {
    struct Object { Counted item{1}; } object;
    auto watcher = vardiff::track(object, vardiff::field("item", &Object::item));
    object.item.data = 2;
    watcher.snapshot();
    CHECK(watcher.undo());
    object.item.data = 3;
    Counted::throw_copy = true;
    expect_throw<std::runtime_error>([&] { watcher.snapshot(); });
    Counted::throw_copy = false;
    CHECK(watcher.history().size() == 2);
    CHECK(watcher.history_position() == 0);
    CHECK(watcher.can_redo());
    CHECK(watcher.diff()[0].before.get<Counted>().data == 1);
    CHECK(watcher.redo());
    CHECK(object.item.data == 2);
}

void comparator_exceptions_do_not_commit() {
    Player player;
    bool fail = false;
    auto watcher = vardiff::track(player, vardiff::field(
        "hp", &Player::hp, [&fail](int a, int b) {
            if (fail) {
                throw std::runtime_error("comparison failed");
            }
            return a == b;
        }));
    player.hp = 75;
    fail = true;
    expect_throw<std::runtime_error>([&] { watcher.poll(); });
    fail = false;
    CHECK(watcher.history().size() == 1);
    CHECK(watcher.poll().size() == 1);
}

void copy_only_custom_types_are_supported() {
    struct Object { CopyOnly item; } object;
    auto watcher = vardiff::track(object, vardiff::field("item", &Object::item));
    object.item.data = 2;
    const auto changes = watcher.poll();
    CHECK(changes[0].before.get<CopyOnly>().data == 1);
    CHECK(changes[0].after.get<CopyOnly>().data == 2);
    CHECK(watcher.undo());
    CHECK(object.item.data == 1);
    auto history = watcher.history();
    CHECK(history.size() == 2);
    CHECK(std::get<0>(history[0]).data == 1);
    CHECK(std::get<0>(history[1]).data == 2);
    std::get<0>(history[0]).data = 99;
    CHECK(std::get<0>(watcher.history()[0]).data == 1);
}

void value_equality_printing_and_missing_values() {
    CHECK(vardiff::value{} == vardiff::value{});
    CHECK(vardiff::value(1) == vardiff::value(1));
    CHECK(!(vardiff::value(1) == vardiff::value(1.0)));
    CHECK(vardiff::value(Profile{}) == vardiff::value(Profile{}));
    CHECK(vardiff::value(std::vector<Profile>{{}}) ==
          vardiff::value(std::vector<Profile>{{}}));
    expect_throw<std::bad_variant_access>([] { (void)vardiff::value{}.get<int>(); });
    expect_throw<std::logic_error>([] {
        (void)(vardiff::value(Opaque{1}) == vardiff::value(Opaque{1}));
    });
    std::ostringstream output;
    output << vardiff::value{} << ' ' << vardiff::value(std::array{1, 2}) << ' '
           << vardiff::value(Profile{}) << ' ' << vardiff::value(Opaque{1});
    CHECK(output.str() == "<missing> [1, 2] {score: 10, label: first} <value>");
}

void invalid_registration_arguments_are_rejected() {
    Player player;
    auto watcher = track_player(player);
    expect_throw<std::invalid_argument>([&] { watcher.ignore(""); });
    expect_throw<std::invalid_argument>([&] { watcher.on_change({}); });
    expect_throw<std::invalid_argument>([&] { watcher.on_change("", [](const auto&) {}); });
    expect_throw<std::invalid_argument>([&] {
        watcher.on_change("hp", std::function<void(const vardiff::change&)>{});
    });
}

} // namespace

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"deferred callbacks", callbacks_are_deferred},
        {"callback order and path filters", callback_order_and_path_filters},
        {"disabled auto snapshot", auto_snapshot_can_be_disabled},
        {"stateful callbacks", stateful_callbacks_keep_their_state},
        {"callback edits stay pending", callback_mutations_remain_pending},
        {"deferred callback registration", registrations_during_dispatch_are_deferred},
        {"callback exception", callback_exception_keeps_baseline},
        {"reentrant operations", recursive_poll_and_history_mutation_are_rejected},
        {"top-level ignores", ignore_top_level_and_prefix_boundaries},
        {"nested ignores", ignore_nested_fields_and_elements},
        {"custom comparator baseline", custom_comparison_uses_original_baseline},
        {"comparator-only custom type", comparator_only_custom_type},
        {"atomic custom comparator", custom_comparator_is_atomic},
        {"owned history snapshots", history_returns_independent_snapshots},
        {"undo redo rollback", undo_redo_and_rollback},
        {"unsaved edits and undo", unsaved_edits_are_not_committed_by_undo},
        {"selected snapshot rollback", rollback_to_a_selected_snapshot},
        {"new branch discards redo", new_snapshot_or_poll_discards_redo},
        {"bounded history", bounded_history_and_limit_changes},
        {"independent objects", independent_objects_and_registered_fields},
        {"vector element changes", vectors_add_remove_and_modify_by_index},
        {"owned added objects", added_registered_container_items_are_owned},
        {"optional transitions", optionals_presence_and_nested_fields},
        {"arrays and nested containers", arrays_and_deep_container_paths},
        {"recursive value trees", recursive_value_trees},
        {"vector bool and empty array", vector_bool_and_empty_array},
        {"nondefault container elements", containers_of_nondefault_constructible_elements},
        {"restore registered members only", restoring_registered_fields_preserves_unregistered_data},
        {"read-only watcher", read_only_objects_can_poll_and_keep_history},
        {"copy-free idle checks", idle_checks_do_not_copy_and_changed_short_circuits},
        {"snapshot copy exception", snapshot_exception_preserves_history_and_redo},
        {"comparator exception", comparator_exceptions_do_not_commit},
        {"copy-only custom types", copy_only_custom_types_are_supported},
        {"type-erased values", value_equality_printing_and_missing_values},
        {"invalid arguments", invalid_registration_arguments_are_rejected},
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
    std::cout << std::size(tests) - failures << "/" << std::size(tests) << " tests passed\n";
    return failures == 0 ? 0 : 1;
}

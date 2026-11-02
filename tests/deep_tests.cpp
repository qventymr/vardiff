// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

#include <vardiff/vardiff.hpp>

#include <iostream>
#include <cmath>
#include <numeric>
#include <random>
#include <set>

#define CHECK(condition) do { if (!(condition)) { \
    throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #condition); \
} } while (false)

struct Item {
    int id;
    int count;
    std::string label = "item";
};

template <> struct vardiff::schema<Item> {
    static auto fields() {
        return std::make_tuple(vardiff::field("id", &Item::id),
            vardiff::field("count", &Item::count), vardiff::field("label", &Item::label));
    }
};

struct State {
    std::vector<Item> inventory{};
    std::map<std::string, std::optional<Item>> slots{};
    std::unordered_map<int, std::vector<Item>> groups{};
};

template <> struct vardiff::schema<State> {
    static auto fields() {
        return std::make_tuple(vardiff::field("inventory", &State::inventory),
            vardiff::field("slots", &State::slots), vardiff::field("groups", &State::groups));
    }
};

struct Node { int value = 0; std::vector<Node> children; };
template <> struct vardiff::schema<Node> {
    static auto fields() {
        return std::make_tuple(vardiff::field("value", &Node::value),
                               vardiff::field("children", &Node::children));
    }
};

struct Bundle {
    int id;
    std::vector<Item> inventory{};
    std::map<int, int> slots{};
    std::optional<int> tag{};
};
template <> struct vardiff::schema<Bundle> {
    static auto fields() {
        return std::make_tuple(vardiff::field("id", &Bundle::id),
            vardiff::field("inventory", &Bundle::inventory),
            vardiff::field("slots", &Bundle::slots), vardiff::field("tag", &Bundle::tag));
    }
};

struct Counted {
    static inline int copies = 0;
    int data = 0;
    Counted() = default;
    explicit Counted(int value) : data(value) {}
    Counted(const Counted& other) : data(other.data) { ++copies; }
    Counted& operator=(const Counted&) = default;
    bool operator==(const Counted&) const = default;
};

struct CountedItem { Counted id; Counted payload; };
template <> struct vardiff::schema<CountedItem> {
    static auto fields() {
        return std::make_tuple(vardiff::field("payload", &CountedItem::payload));
    }
};
template <> struct std::hash<Counted> {
    std::size_t operator()(const Counted& value) const noexcept {
        return static_cast<std::size_t>(value.data % 2); // Deliberate collisions.
    }
};

struct EqualityKey { int data; bool operator==(const EqualityKey&) const = default; };
struct Unhashed { EqualityKey id; int count; };
template <> struct vardiff::schema<Unhashed> {
    static auto fields() {
        return std::make_tuple(vardiff::field("count", &Unhashed::count));
    }
};

struct Atomic { std::vector<int> items; };
template <> struct vardiff::schema<Atomic> {
    static auto fields() {
        return std::make_tuple(vardiff::field("items", &Atomic::items,
            [](const auto& a, const auto& b) { return a.size() == b.size(); }));
    }
};

struct CopyOnly {
    int data;
    explicit CopyOnly(int value) : data(value) {}
    CopyOnly(const CopyOnly&) = default;
    CopyOnly(CopyOnly&&) = delete;
    CopyOnly& operator=(const CopyOnly&) = default;
    bool operator==(const CopyOnly&) const = default;
};
struct CopyOnlyItem { CopyOnly id; int count; };
template <> struct vardiff::schema<CopyOnlyItem> {
    static auto fields() { return std::make_tuple(vardiff::field("count", &CopyOnlyItem::count)); }
};

struct Throwing { int value; };
template <> struct vardiff::schema<Throwing> {
    static auto fields() {
        return std::make_tuple(vardiff::field("value", &Throwing::value,
            [](int, int) -> bool { throw std::runtime_error("comparison failed"); }));
    }
};

namespace {
using vardiff::change_kind;
using Changes = std::vector<vardiff::deep_change>;

const vardiff::deep_change& find(const Changes& changes, std::string_view path,
                               change_kind kind = change_kind::modified) {
    const auto found = std::find_if(changes.begin(), changes.end(), [&](const auto& c) {
        return c.name == path && c.kind == kind;
    });
    CHECK(found != changes.end());
    return *found;
}

std::size_t count(const Changes& changes, change_kind kind) {
    return static_cast<std::size_t>(std::count_if(changes.begin(), changes.end(),
        [&](const auto& c) { return c.kind == kind; }));
}

template <typename Exception, typename Function> void throws(Function function) {
    bool caught = false;
    try { function(); } catch (const Exception&) { caught = true; }
    CHECK(caught);
}

void root_values_and_empty_containers() {
    CHECK(vardiff::deep_diff(1, 1).empty());
    const auto changes = vardiff::deep_diff(1, 2);
    CHECK(changes.size() == 1 && changes[0].name.empty());
    CHECK(changes[0].before.get<int>() == 1 && changes[0].after.get<int>() == 2);
    CHECK(vardiff::deep_diff(State{}, State{}).empty());
    CHECK(vardiff::deep_diff(std::array<int, 0>{}, std::array<int, 0>{}).empty());
    const std::vector<int> empty;
    const auto added = vardiff::deep_diff(empty, std::vector{1, 2});
    CHECK(added.size() == 2 && count(added, change_kind::added) == 2);
    CHECK(!added[0].before.has_value() && added[0].after.get<int>() == 1);
    const auto removed = vardiff::deep_diff(std::vector{1, 2}, empty);
    CHECK(count(removed, change_kind::removed) == 2);
    CHECK(removed[1].before.get<int>() == 2 && !removed[1].after.has_value());
}

void recursive_registered_paths() {
    const State before{{{1, 2}, {2, 3}, {3, 4}}, {{"main", Item{7, 8}}}, {{4, {{9, 10}}}}};
    auto after = before;
    after.inventory[2].count = 20;
    after.slots.at("main")->label = "changed";
    after.groups.at(4)[0].count = 30;
    const auto changes = vardiff::deep_diff(before, after, vardiff::key(&Item::id));
    CHECK(changes.size() == 3);
    CHECK(find(changes, "inventory[2].count").after.get<int>() == 20);
    CHECK(find(changes, "slots[\"main\"].label").after.get<std::string>() == "changed");
    CHECK(find(changes, "groups[4][0].count").after.get<int>() == 30);
}

void keyed_rotation_and_modified_move() {
    const std::vector<Item> before{{1, 10}, {2, 20}, {3, 30}};
    const std::vector<Item> after{{2, 20}, {3, 30}, {1, 99}};
    const auto changes = vardiff::deep_diff(before, after, vardiff::key(&Item::id));
    CHECK(changes.size() == 2);
    const auto& move = find(changes, "[2]", change_kind::moved);
    CHECK(move.from == "[0]" && move.old_index == 0 && move.new_index == 2);
    CHECK(!move.before.has_value() && !move.after.has_value());
    const auto& edit = find(changes, "[2].count");
    CHECK(edit.before.get<int>() == 10 && edit.after.get<int>() == 99);
    CHECK(!edit.old_index && edit.from.empty());
}

void insertions_and_removals_do_not_move_survivors() {
    const std::vector<Item> before{{1, 10}, {2, 20}};
    const auto added = vardiff::deep_diff(before, std::vector<Item>{{0, 0}, {1, 10}, {2, 20}},
                                         vardiff::key(&Item::id));
    CHECK(added.size() == 1 && added[0].kind == change_kind::added && added[0].name == "[0]");
    const auto removed = vardiff::deep_diff(before, std::vector<Item>{{2, 20}}, vardiff::key(&Item::id));
    CHECK(removed.size() == 1 && removed[0].kind == change_kind::removed && removed[0].name == "[0]");
}

void changed_identity_is_remove_add() {
    const auto changes = vardiff::deep_diff(std::vector<Item>{{1, 4}}, std::vector<Item>{{2, 4}},
                                           vardiff::key(&Item::id));
    CHECK(changes.size() == 2);
    CHECK(changes[0].kind == change_kind::removed && changes[1].kind == change_kind::added);
}

void unregistered_identity_is_observed() {
    const std::vector<CountedItem> before{{Counted{1}, Counted{4}}};
    const std::vector<CountedItem> after{{Counted{2}, Counted{4}}};
    const auto changes = vardiff::deep_diff(before, after, vardiff::key(&CountedItem::id));
    CHECK(count(changes, change_kind::added) == 1 && count(changes, change_kind::removed) == 1);
}

void duplicate_keys_match_fifo() {
    const std::vector<Item> before{{1, 10}, {1, 20}, {2, 30}};
    const std::vector<Item> after{{1, 11}, {2, 30}, {1, 22}, {1, 40}};
    const auto changes = vardiff::deep_diff(before, after, vardiff::key(&Item::id));
    CHECK(find(changes, "[0].count").before.get<int>() == 10);
    CHECK(find(changes, "[2].count").before.get<int>() == 20);
    CHECK(count(changes, change_kind::moved) == 1);
    CHECK(find(changes, "[3]", change_kind::added).after.get<Item>().count == 40);
}

void duplicate_keys_can_be_rejected() {
    vardiff::deep_options strict;
    strict.duplicates = vardiff::duplicate_key_policy::reject;
    const auto key = vardiff::key(&Item::id);
    const std::vector<Item> duplicate{{1, 1}, {1, 2}}, single{{1, 1}}, empty;
    throws<std::invalid_argument>([&] { (void)vardiff::deep_diff(duplicate, duplicate, strict, key); });
    throws<std::invalid_argument>([&] { (void)vardiff::deep_diff(duplicate, empty, strict, key); });
    throws<std::invalid_argument>([&] { (void)vardiff::deep_diff(single, duplicate, strict, key); });
    throws<std::invalid_argument>([&] { (void)vardiff::deep_diff(empty, duplicate, strict, key); });
    CHECK(vardiff::deep_diff(single, single, strict, key).empty());
}

void no_move_mode_keeps_key_matching() {
    const std::vector<Item> before{{1, 10}, {2, 20}}, after{{2, 21}, {1, 10}};
    vardiff::deep_options options;
    options.detect_moves = false;
    const auto changes = vardiff::deep_diff(before, after, options, vardiff::key(&Item::id));
    CHECK(changes.size() == 1 && changes[0].name == "[0].count");
    CHECK(changes[0].before.get<int>() == 20);
    const auto indexed = vardiff::deep_diff(std::vector{1, 2}, std::vector{2, 1}, options);
    CHECK(indexed.size() == 2 && count(indexed, change_kind::modified) == 2);
}

void unkeyed_sequences_detect_moves_and_edits() {
    CHECK(count(vardiff::deep_diff(std::vector{1, 2, 3}, std::vector{2, 3, 1}), change_kind::moved) == 1);
    const auto edit = vardiff::deep_diff(std::vector{1, 2, 3}, std::vector{1, 9, 3});
    CHECK(edit.size() == 1 && edit[0].name == "[1]" && edit[0].kind == change_kind::modified);
    const auto shifted = vardiff::deep_diff(std::vector{1, 2}, std::vector{2, 3});
    CHECK(shifted.size() == 2 && count(shifted, change_kind::moved) == 0);
    CHECK(count(vardiff::deep_diff(std::vector{1, 1, 2}, std::vector{1, 2, 1}), change_kind::moved) == 1);
    CHECK(count(vardiff::deep_diff(std::vector<std::string>{"a", "b"},
                                  std::vector<std::string>{"b", "a"}), change_kind::moved) == 1);
}

void arrays_vector_bool_and_unhashable_elements() {
    CHECK(count(vardiff::deep_diff(std::array{1, 2}, std::array{2, 1}), change_kind::moved) == 1);
    CHECK(count(vardiff::deep_diff(std::vector<bool>{true, false}, std::vector<bool>{false, true}),
                change_kind::moved) == 1);
    CHECK(count(vardiff::deep_diff(std::vector<Item>{{1, 1}, {2, 2}},
                                  std::vector<Item>{{2, 2}, {1, 1}}), change_kind::moved) == 1);
    const std::array<Item, 2> a{{{1, 1}, {2, 2}}}, b{{{2, 2}, {1, 1}}};
    CHECK(count(vardiff::deep_diff(a, b, vardiff::key(&Item::id)), change_kind::moved) == 1);
}

void equality_only_keys_and_collisions() {
    const std::vector<Unhashed> a{{{1}, 10}, {{2}, 20}}, b{{{2}, 21}, {{1}, 10}};
    const auto changes = vardiff::deep_diff(a, b, vardiff::key(&Unhashed::id));
    CHECK(count(changes, change_kind::moved) == 1 && find(changes, "[0].count").before.get<int>() == 20);
    vardiff::deep_options strict;
    strict.duplicates = vardiff::duplicate_key_policy::reject;
    throws<std::invalid_argument>([&] {
        (void)vardiff::deep_diff(std::vector<Unhashed>{{{1}, 1}, {{1}, 2}}, b, strict,
                                vardiff::key(&Unhashed::id));
    });
    throws<std::invalid_argument>([&] {
        (void)vardiff::deep_diff(a, std::vector<Unhashed>{{{3}, 1}, {{3}, 2}}, strict,
                                vardiff::key(&Unhashed::id));
    });
    const std::vector<CountedItem> x{{Counted{1}, Counted{10}}, {Counted{3}, Counted{20}}};
    const std::vector<CountedItem> y{{Counted{3}, Counted{21}}, {Counted{1}, Counted{10}}};
    const auto collision = vardiff::deep_diff(x, y, vardiff::key(&CountedItem::id));
    CHECK(count(collision, change_kind::moved) == 1);
    CHECK(find(collision, "[0].payload").before.get<Counted>().data == 20);
}

void path_specific_key_overrides_type_key() {
    // Matching by count at this path makes the id change a leaf modification.
    const State a{{{1, 10}}}, b{{{2, 10}}};
    const auto global = vardiff::key(&Item::id);
    const auto local = vardiff::key("inventory", &Item::count);
    const auto changes = vardiff::deep_diff(a, b, global, local);
    CHECK(changes.size() == 1 && changes[0].name == "inventory[0].id");
    CHECK(vardiff::deep_diff(a, b, local, global).size() == 1);
}

void map_add_remove_modify_and_escaped_keys() {
    const std::map<std::string, Item> a{{"keep", {1, 10}}, {"remove", {2, 20}}};
    const std::map<std::string, Item> b{{"keep", {1, 11}}, {"add", {3, 30}}};
    const auto changes = vardiff::deep_diff(a, b);
    CHECK(changes.size() == 3);
    CHECK(find(changes, "[\"keep\"].count").before.get<int>() == 10);
    CHECK(find(changes, "[\"remove\"]", change_kind::removed).before.get<Item>().id == 2);
    CHECK(find(changes, "[\"add\"]", change_kind::added).after.get<Item>().id == 3);
    const std::string escaped = "a\"\\]\n";
    const auto special = vardiff::deep_diff(std::map<std::string, int>{{escaped, 1}},
                                           std::map<std::string, int>{{escaped, 2}});
    CHECK(special[0].name == "[\"a\\\"\\\\]\\u000a\"]");
    CHECK(vardiff::deep_diff(std::map<int, int>{}, std::map<int, int>{{-12, 1}})[0].name == "[-12]");
}

void unordered_map_rehash_is_not_a_change() {
    std::unordered_map<int, Item> a{{1, {1, 1}}, {2, {2, 2}}};
    auto b = a;
    b.reserve(128);
    CHECK(vardiff::deep_diff(a, b).empty());
    b.at(2).count = 9;
    b.erase(1);
    b.emplace(3, Item{3, 3});
    const auto changes = vardiff::deep_diff(a, b);
    CHECK(changes.size() == 3);
    CHECK(find(changes, "[2].count").after.get<int>() == 9);
    CHECK(count(changes, change_kind::moved) == 0);
}

void map_uses_native_key_equivalence() {
    struct Fold {
        bool operator()(const std::string& a, const std::string& b) const {
            const auto lower = [](const std::string& text) {
                auto result = text;
                for (auto& c : result) { if (c >= 'A' && c <= 'Z') { c += 'a' - 'A'; } }
                return result;
            };
            return lower(a) < lower(b);
        }
    };
    const std::map<std::string, int, Fold> a{{"ABC", 1}}, b{{"abc", 2}};
    const auto changes = vardiff::deep_diff(a, b);
    CHECK(changes.size() == 1 && changes[0].kind == change_kind::modified);
    CHECK(changes[0].name == "[\"abc\"]");
}

void optionals_and_container_combinations() {
    const std::optional<Item> empty, item{Item{1, 2}};
    const auto added = vardiff::deep_diff(empty, item);
    CHECK(added.size() == 1 && added[0].kind == change_kind::added && added[0].after.get<Item>().count == 2);
    CHECK(vardiff::deep_diff(item, empty)[0].kind == change_kind::removed);
    using Nested = std::vector<std::optional<std::map<int, std::vector<Item>>>>;
    const Nested a{{std::map<int, std::vector<Item>>{{7, {{1, 2}, {2, 3}}}}}};
    auto b = a;
    auto& values = b[0]->at(7);
    std::reverse(values.begin(), values.end());
    values[0].count = 8;
    const auto changes = vardiff::deep_diff(a, b, vardiff::key(&Item::id));
    CHECK(count(changes, change_kind::moved) == 1);
    CHECK(find(changes, "[0][7][0].count").before.get<int>() == 3);
    using OptionalVector = std::vector<std::optional<int>>;
    const auto present = vardiff::deep_diff(OptionalVector{}, OptionalVector{std::nullopt});
    CHECK(present[0].after.has_value() && !present[0].after.get<std::optional<int>>());
}

void ignore_paths_and_boundaries() {
    const State a{{{1, 1}, {2, 2}}}, b{{{2, 9}, {1, 1}}};
    vardiff::deep_options options;
    options.ignored_paths = {"inventory[0].count"};
    const auto changes = vardiff::deep_diff(a, b, options, vardiff::key(&Item::id));
    CHECK(count(changes, change_kind::modified) == 0);
    options.ignored_paths = {"inventory"};
    CHECK(vardiff::deep_diff(a, b, options, vardiff::key(&Item::id)).empty());
    options.ignored_paths = {"invent"};
    CHECK(!vardiff::deep_diff(a, b, options, vardiff::key(&Item::id)).empty());
    options.ignored_paths = {"[\"x\"]"};
    const auto map = vardiff::deep_diff(std::map<std::string, int>{{"x", 1}, {"xx", 1}},
                                       std::map<std::string, int>{{"x", 2}, {"xx", 2}}, options);
    CHECK(map.size() == 1 && map[0].name == "[\"xx\"]");
    options.ignored_paths = {""};
    throws<std::invalid_argument>([&] { (void)vardiff::deep_diff(1, 2, options); });
}

void bounded_deep_recursion() {
    Node a;
    auto* node = &a;
    for (int i = 0; i < 80; ++i) {
        node->children.push_back(Node{});
        node = &node->children[0];
    }
    auto b = a;
    node = &b;
    for (int i = 0; i < 80; ++i) { node = &node->children[0]; }
    node->value = 42;
    vardiff::deep_options options;
    options.detect_moves = false;
    const auto changes = vardiff::deep_diff(a, b, options);
    CHECK(changes.size() == 1 && changes[0].after.get<int>() == 42);
    options.max_depth = 32;
    throws<std::length_error>([&] { (void)vardiff::deep_diff(a, b, options); });
    throws<std::length_error>([&] { (void)vardiff::deep_diff(a, a, options); });
    options.max_depth = 0;
    CHECK(vardiff::deep_diff(1, 2, options).size() == 1);
    throws<std::length_error>([&] { (void)vardiff::deep_diff(std::vector{1}, std::vector{2}, options); });
}

void no_copies_for_idle_checks_matching_and_moves() {
    const std::vector<CountedItem> a{{Counted{1}, Counted{10}}, {Counted{3}, Counted{20}}};
    const std::vector<CountedItem> b{{Counted{3}, Counted{20}}, {Counted{1}, Counted{10}}};
    const auto key = vardiff::key(&CountedItem::id);
    Counted::copies = 0;
    CHECK(vardiff::deep_diff(a, a, key).empty());
    CHECK(Counted::copies == 0);
    const auto changes = vardiff::deep_diff(a, b, key);
    CHECK(count(changes, change_kind::moved) == 1 && Counted::copies == 0);
    auto modified = b;
    modified[0].payload.data = 21;
    Counted::copies = 0;
    const auto edits = vardiff::deep_diff(a, modified, key);
    CHECK(Counted::copies == 2); // Only before/after of the modified leaf.
    CHECK(find(edits, "[0].payload").after.get<Counted>().data == 21);
}

void returned_events_own_their_values() {
    const auto events = [] {
        const std::vector<Item> a{{1, 1}}, b{{1, 2}, {2, 3}};
        return vardiff::deep_diff(a, b, vardiff::key(&Item::id));
    }();
    CHECK(find(events, "[0].count").before.get<int>() == 1);
    CHECK(find(events, "[1]", change_kind::added).after.get<Item>().count == 3);
}

void nested_moves_keep_original_source_paths() {
    const std::vector<Bundle> a{{10, {{1, 1}, {2, 2}, {3, 3}}, {{1, 1}}, 7}, {20}, {30}};
    auto b = a;
    std::rotate(b.begin(), b.begin() + 1, b.end());
    std::rotate(b[2].inventory.begin(), b[2].inventory.begin() + 1, b[2].inventory.end());
    b[2].inventory[2].count = 99;
    b[2].slots.clear();
    b[2].tag.reset();
    const auto changes = vardiff::deep_diff(a, b, vardiff::key(&Bundle::id), vardiff::key(&Item::id));
    CHECK(find(changes, "[2]", change_kind::moved).from == "[0]");
    CHECK(find(changes, "[2].inventory[2]", change_kind::moved).from == "[0].inventory[0]");
    CHECK(find(changes, "[2].inventory[2].count").before.get<int>() == 1);
    CHECK(find(changes, "[0].slots[1]", change_kind::removed).before.get<int>() == 1);
    CHECK(find(changes, "[0].tag", change_kind::removed).before.get<int>() == 7);
    CHECK(changes.size() == 5);
    vardiff::deep_options options;
    options.ignored_paths = {"[0].slots[1]", "[0].tag"};
    const auto filtered = vardiff::deep_diff(a, b, options, vardiff::key(&Bundle::id), vardiff::key(&Item::id));
    CHECK(filtered.size() == 3);
}

void floating_values_keep_existing_equality() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto changes = vardiff::deep_diff(std::vector{nan, 0.0}, std::vector{nan, -0.0});
    CHECK(changes.size() == 1 && changes[0].kind == change_kind::modified);
    CHECK(std::isnan(changes[0].before.get<double>()));
}

void legacy_map_watchers_and_history() {
    State state{{}, {{"main", Item{1, 2}}}, {{7, {{2, 3}}}}};
    auto watcher = vardiff::track(state);
    state.slots.at("main")->count = 9;
    state.groups.emplace(8, std::vector<Item>{{3, 4}});
    CHECK(watcher.changed());
    const auto changes = watcher.poll();
    CHECK(changes.size() == 2 && !watcher.changed());
    CHECK(watcher.undo() && state.slots.at("main")->count == 2 && state.groups.size() == 1);
    CHECK(watcher.redo() && state.groups.size() == 2);
    watcher.ignore("slots[\"main\"].label");
    state.slots.at("main")->label = "preserve";
    CHECK(!watcher.changed());
    CHECK(watcher.undo() && state.slots.at("main")->label == "preserve");
    CHECK(vardiff::changed(State{}, state));
    CHECK(!vardiff::diff(State{}, state).empty());
    std::ostringstream stream;
    stream << vardiff::value(std::map<int, int>{{1, 2}});
    CHECK(stream.str() == "{1: 2}");
}

void map_ignore_preserves_whole_entries() {
    struct Object { std::map<int, Item> items; } object{{{1, {1, 1}}}};
    auto watcher = vardiff::track(object, vardiff::field("items", &Object::items));
    watcher.ignore("items[1]");
    object.items.erase(1);
    object.items.emplace(2, Item{2, 2});
    watcher.snapshot();
    object.items.emplace(1, Item{1, 9});
    CHECK(watcher.undo());
    CHECK(object.items.size() == 1 && object.items.at(1).count == 9);
}

void custom_comparators_remain_atomic() {
    const Atomic a{{1, 2}}, b{{2, 1}};
    const auto field = vardiff::field("items", &Atomic::items,
        [](const auto& x, const auto& y) { return x.size() == y.size(); });
    CHECK(vardiff::diff(a, b, field).empty());
    CHECK(vardiff::deep_diff(a, b).empty());
    const auto changes = vardiff::deep_diff(a, Atomic{{1, 2, 3}});
    CHECK(changes.size() == 1 && changes[0].name == "items");
    CHECK(changes[0].before.get<std::vector<int>>() == a.items);
    CHECK(changes[0].kind == change_kind::modified);
}

void copy_only_and_nondefault_values() {
    const std::vector<CopyOnlyItem> a{{CopyOnly{1}, 10}, {CopyOnly{2}, 20}};
    const std::vector<CopyOnlyItem> b{{CopyOnly{2}, 21}, {CopyOnly{1}, 10}};
    const auto changes = vardiff::deep_diff(a, b, vardiff::key(&CopyOnlyItem::id));
    CHECK(count(changes, change_kind::moved) == 1);
    CHECK(find(changes, "[0].count").before.get<int>() == 20);
    const std::map<int, CopyOnly> x{{1, CopyOnly{1}}}, y{{1, CopyOnly{2}}, {2, CopyOnly{3}}};
    const auto mapped = vardiff::deep_diff(x, y);
    CHECK(find(mapped, "[1]").after.get<CopyOnly>().data == 2);
    CHECK(find(mapped, "[2]", change_kind::added).after.get<CopyOnly>().data == 3);
}

void map_sizes_and_numeric_key_paths() {
    const std::map<int, int> empty;
    const std::map<int, int> full{{1, 2}, {3, 4}};
    CHECK(count(vardiff::deep_diff(empty, full), change_kind::added) == 2);
    CHECK(count(vardiff::deep_diff(full, empty), change_kind::removed) == 2);
    const std::unordered_map<int, int> unordered{{1, 2}};
    CHECK(count(vardiff::deep_diff(std::unordered_map<int, int>{}, unordered), change_kind::added) == 1);
    CHECK(count(vardiff::deep_diff(unordered, std::unordered_map<int, int>{}), change_kind::removed) == 1);
    enum class Number { negative = -2 };
    const auto enums = vardiff::deep_diff(std::map<Number, int>{{Number::negative, 1}},
                                         std::map<Number, int>{{Number::negative, 2}});
    CHECK(enums[0].name == "[-2]");
    CHECK(vardiff::deep_diff(std::map<bool, int>{{true, 1}},
                             std::map<bool, int>{{true, 2}})[0].name == "[true]");
    using Integer = unsigned long long;
    constexpr auto largest = std::numeric_limits<Integer>::max();
    const auto large = vardiff::deep_diff(std::map<Integer, int>{{largest, 1}},
                                         std::map<Integer, int>{{largest, 2}});
    CHECK(large[0].name == "[" + std::to_string(largest) + "]");
}

void unordered_map_nested_history_preserves_unregistered_fields() {
    std::unordered_map<int, CountedItem> initial{{1, {Counted{1}, Counted{10}}}};
    struct Object { std::unordered_map<int, CountedItem> items; } object{initial};
    auto watcher = vardiff::track(object, vardiff::field("items", &Object::items));
    object.items.at(1).payload.data = 20;
    object.items.emplace(2, CountedItem{Counted{2}, Counted{30}});
    watcher.snapshot();
    object.items.at(1).id.data = 100; // Unregistered metadata survives restoration.
    CHECK(watcher.undo());
    CHECK(object.items.size() == 1 && object.items.at(1).payload.data == 10);
    CHECK(object.items.at(1).id.data == 100);
    CHECK(watcher.redo() && object.items.size() == 2);
}

void exceptions_do_not_change_inputs() {
    const std::vector<Throwing> a{{1}}, b{{2}};
    throws<std::runtime_error>([&] { (void)vardiff::deep_diff(a, b); });
    CHECK(a[0].value == 1 && b[0].value == 2);
    vardiff::deep_options options;
    options.ignored_paths = {"[0]"};
    CHECK(vardiff::deep_diff(a, b, options, vardiff::key(&Throwing::value)).empty());
}

void legacy_aggregate_and_index_semantics() {
    // Existing four-element structured bindings must still compile.
    const vardiff::change event{"field", vardiff::value(1), vardiff::value(2)};
    const auto& [name, before, after, kind] = event;
    CHECK(name == "field" && before.get<int>() == 1 && after.get<int>() == 2);
    CHECK(kind == change_kind::modified);
    const State a{{{1, 10}, {2, 20}}}, b{{{2, 20}, {1, 10}}};
    const auto changes = vardiff::diff(a, b);
    CHECK(changes.size() == 4);
    CHECK(std::none_of(changes.begin(), changes.end(), [](const auto& c) { return c.kind == change_kind::moved; }));
    static_assert(static_cast<int>(change_kind::modified) == 0);
    static_assert(static_cast<int>(change_kind::added) == 1);
    static_assert(static_cast<int>(change_kind::removed) == 2);
}

void bad_keys_are_rejected() {
    throws<std::invalid_argument>([] { (void)vardiff::key(static_cast<int Item::*>(nullptr)); });
    throws<std::invalid_argument>([] { (void)vardiff::key("", &Item::id); });
}

std::size_t lis_length(const std::vector<int>& values) {
    std::vector<std::size_t> lengths(values.size(), 1);
    std::size_t longest = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (values[j] < values[i]) { lengths[i] = std::max(lengths[i], lengths[j] + 1); }
        }
        longest = std::max(longest, lengths[i]);
    }
    return longest;
}

void exhaustive_permutation_move_counts() {
    const std::vector<int> before{0, 1, 2, 3, 4, 5, 6};
    auto after = before;
    do {
        const auto changes = vardiff::deep_diff(before, after);
        CHECK(changes.size() == before.size() - lis_length(after));
        for (const auto& change : changes) {
            CHECK(change.kind == change_kind::moved && change.old_index && change.new_index);
            CHECK(before[*change.old_index] == after[*change.new_index]);
        }
    } while (std::next_permutation(after.begin(), after.end()));
}

void randomized_keyed_edit_oracle() {
    std::mt19937 random(42);
    for (int iteration = 0; iteration < 200; ++iteration) {
        std::vector<Item> before, after;
        for (int i = 0; i < 24; ++i) { before.push_back(Item{i, i}); }
        after = before;
        std::shuffle(after.begin(), after.end(), random);
        after.erase(after.begin(), after.begin() + 4);
        after.push_back(Item{30, 30});
        for (std::size_t i = 0; i < after.size(); i += 3) { after[i].count += 100; }
        const auto changes = vardiff::deep_diff(before, after, vardiff::key(&Item::id));
        CHECK(count(changes, change_kind::removed) == 4 && count(changes, change_kind::added) == 1);
        CHECK(count(changes, change_kind::modified) == 7);
        std::vector<int> survivors;
        for (std::size_t j = 0; j + 1 < after.size(); ++j) {
            survivors.push_back(after[j].id);
            if (j % 3 == 0) {
                const auto& edit = find(changes, "[" + std::to_string(j) + "].count");
                CHECK(edit.before.get<int>() == after[j].id && edit.after.get<int>() == after[j].count);
            }
        }
        CHECK(count(changes, change_kind::moved) == survivors.size() - lis_length(survivors));
    }
}
} // namespace

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"roots and empty containers", root_values_and_empty_containers},
        {"registered nested paths", recursive_registered_paths},
        {"keyed rotation with edit", keyed_rotation_and_modified_move},
        {"insert/remove shifts", insertions_and_removals_do_not_move_survivors},
        {"changed identity", changed_identity_is_remove_add},
        {"unregistered key", unregistered_identity_is_observed},
        {"duplicate FIFO", duplicate_keys_match_fifo},
        {"strict duplicates", duplicate_keys_can_be_rejected},
        {"disable moves", no_move_mode_keeps_key_matching},
        {"unkeyed moves and edits", unkeyed_sequences_detect_moves_and_edits},
        {"array, vector bool, unhashed values", arrays_vector_bool_and_unhashable_elements},
        {"equality keys and hash collisions", equality_only_keys_and_collisions},
        {"path selector precedence", path_specific_key_overrides_type_key},
        {"map paths", map_add_remove_modify_and_escaped_keys},
        {"unordered map rehash", unordered_map_rehash_is_not_a_change},
        {"native map equivalence", map_uses_native_key_equivalence},
        {"optional combinations", optionals_and_container_combinations},
        {"ignored paths", ignore_paths_and_boundaries},
        {"depth bound", bounded_deep_recursion},
        {"copy counts", no_copies_for_idle_checks_matching_and_moves},
        {"owned results", returned_events_own_their_values},
        {"nested move source paths", nested_moves_keep_original_source_paths},
        {"NaN and signed zero", floating_values_keep_existing_equality},
        {"legacy map history", legacy_map_watchers_and_history},
        {"ignored map entries", map_ignore_preserves_whole_entries},
        {"atomic comparators", custom_comparators_remain_atomic},
        {"copy-only/nondefault values", copy_only_and_nondefault_values},
        {"map empty and numeric keys", map_sizes_and_numeric_key_paths},
        {"unordered nested history", unordered_map_nested_history_preserves_unregistered_fields},
        {"exception input preservation", exceptions_do_not_change_inputs},
        {"legacy aggregates and indexing", legacy_aggregate_and_index_semantics},
        {"bad keys", bad_keys_are_rejected},
        {"all 5040 permutations", exhaustive_permutation_move_counts},
        {"randomized keyed edits", randomized_keyed_edit_oracle},
    };
    int failures = 0;
    for (const auto& test : tests) {
        try { test.run(); std::cout << "PASS: " << test.name << '\n'; }
        catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << test.name << ": " << error.what() << '\n';
        }
    }
    std::cout << std::size(tests) - failures << '/' << std::size(tests) << " tests passed\n";
    return failures == 0 ? 0 : 1;
}

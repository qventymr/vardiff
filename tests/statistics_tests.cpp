// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

#include <vardiff/vardiff.hpp>

#include <iostream>

#define CHECK(condition) do { if (!(condition)) { \
    throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #condition); \
} } while (false)

struct Item { int id; int count; };
template <> struct vardiff::schema<Item> {
    static auto fields() {
        return std::make_tuple(vardiff::field("id", &Item::id),
                               vardiff::field("count", &Item::count));
    }
};

namespace {

template <typename T>
concept can_summarize = requires(const T& changes) { vardiff::summarize(changes); };

static_assert(can_summarize<std::vector<vardiff::change>>);
static_assert(can_summarize<std::vector<vardiff::deep_change>>);
static_assert(!can_summarize<std::vector<int>>);
static_assert(noexcept(vardiff::summarize(std::declval<const std::vector<vardiff::change>&>())));
static_assert(noexcept(vardiff::summarize(std::declval<const std::vector<vardiff::deep_change>&>())));
static_assert(std::same_as<decltype(vardiff::diff_statistics::total), std::size_t>);

void expect(const vardiff::diff_statistics& stats, std::size_t added,
            std::size_t removed, std::size_t modified, std::size_t moved) {
    CHECK(stats.added == added);
    CHECK(stats.removed == removed);
    CHECK(stats.modified == modified);
    CHECK(stats.moved == moved);
    CHECK(stats.total == added + removed + modified + moved);
}

void empty_results() {
    expect(vardiff::diff_statistics{}, 0, 0, 0, 0);
    expect(vardiff::summarize(std::vector<vardiff::change>{}), 0, 0, 0, 0);
    expect(vardiff::summarize(std::vector<vardiff::deep_change>{}), 0, 0, 0, 0);
    expect(vardiff::summarize(vardiff::diff(Item{1, 2}, Item{1, 2})), 0, 0, 0, 0);
    expect(vardiff::summarize(vardiff::deep_diff(42, 42)), 0, 0, 0, 0);
}

template <typename Change>
void all_kinds() {
    using vardiff::change_kind;
    const change_kind kinds[]{change_kind::added, change_kind::removed,
                              change_kind::modified, change_kind::moved};
    for (std::size_t i = 0; i < std::size(kinds); ++i) {
        Change event;
        event.kind = kinds[i];
        const std::vector<Change> changes(3, event);
        expect(vardiff::summarize(changes), i == 0 ? 3 : 0, i == 1 ? 3 : 0,
               i == 2 ? 3 : 0, i == 3 ? 3 : 0);
    }
    std::vector<Change> mixed;
    for (std::size_t i = 0; i < std::size(kinds); ++i) {
        Change event;
        event.kind = kinds[i];
        mixed.insert(mixed.end(), i + 1, event);
    }
    expect(vardiff::summarize(mixed), 1, 2, 3, 4);
}

void flat_diff_containers() {
    struct State {
        std::vector<int> values;
        std::optional<int> selected;
        std::map<int, int> scores;
    };
    const State before{{1, 2}, 5, {{1, 1}, {2, 2}}};
    const State after{{1, 9, 3}, std::nullopt, {{1, 3}, {3, 3}, {4, 4}}};
    const auto changes = vardiff::diff(before, after,
        vardiff::field("values", &State::values),
        vardiff::field("selected", &State::selected),
        vardiff::field("scores", &State::scores));
    expect(vardiff::summarize(changes), 3, 2, 2, 0);
    CHECK(changes.size() == 7);
    CHECK(before.values[1] == 2 && after.values[1] == 9);
}

void keyed_moves_and_edits() {
    const std::vector<Item> before{{1, 10}, {2, 20}, {3, 30}, {4, 40}};
    const std::vector<Item> after{{2, 20}, {3, 30}, {1, 11}, {5, 50}};
    const auto changes = vardiff::deep_diff(before, after, vardiff::key(&Item::id));
    expect(vardiff::summarize(changes), 1, 1, 1, 1);
    CHECK(changes.size() == 4);
    CHECK(changes[1].kind == vardiff::change_kind::moved);
    CHECK(changes[1].from == "[0]" && changes[1].name == "[2]");
    CHECK(changes[1].old_index == 0 && changes[1].new_index == 2);
    CHECK(changes[2].before.get<int>() == 10 && changes[2].after.get<int>() == 11);
    expect(vardiff::summarize(changes), 1, 1, 1, 1);
}

void options_filter_existing_records() {
    const std::vector<Item> before{{1, 10}, {2, 20}, {3, 30}};
    const std::vector<Item> after{{2, 20}, {3, 30}, {1, 11}};
    vardiff::deep_options options;
    options.detect_moves = false;
    expect(vardiff::summarize(vardiff::deep_diff(
        before, after, options, vardiff::key(&Item::id))), 0, 0, 1, 0);
    options.ignored_paths = {"[2].count"};
    expect(vardiff::summarize(vardiff::deep_diff(
        before, after, options, vardiff::key(&Item::id))), 0, 0, 0, 0);
    options.detect_moves = true;
    expect(vardiff::summarize(vardiff::deep_diff(
        before, after, options, vardiff::key(&Item::id))), 0, 0, 0, 1);
}

void watcher_baseline_and_callbacks() {
    Item item{1, 10};
    auto watcher = vardiff::track(item);
    int callbacks = 0;
    watcher.on_change([&](const auto& changes) {
        ++callbacks;
        expect(vardiff::summarize(changes), 0, 0, 1, 0);
    });
    item.count = 11;
    expect(vardiff::summarize(watcher.diff()), 0, 0, 1, 0);
    expect(vardiff::summarize(watcher.diff()), 0, 0, 1, 0);
    CHECK(watcher.changed() && watcher.history_size() == 1 && callbacks == 0);
    expect(vardiff::summarize(watcher.poll()), 0, 0, 1, 0);
    CHECK(!watcher.changed() && watcher.history_size() == 2 && callbacks == 1);
}

struct Counted {
    static inline int copies = 0;
    static inline int comparisons = 0;
    int data;
    explicit Counted(int value) : data(value) {}
    Counted(const Counted& other) : data(other.data) { ++copies; }
    bool operator==(const Counted& other) const {
        ++comparisons;
        return data == other.data;
    }
};

template <typename Change>
void no_value_copies_or_comparisons() {
    Change event;
    event.name = "payload";
    event.before = vardiff::value(Counted{1});
    event.after = vardiff::value(Counted{2});
    const std::vector<Change> changes{event};
    const auto* before = &changes[0].before.template get<Counted>();
    const auto* after = &changes[0].after.template get<Counted>();
    Counted::copies = Counted::comparisons = 0;
    expect(vardiff::summarize(changes), 0, 0, 1, 0);
    expect(vardiff::summarize(changes), 0, 0, 1, 0);
    CHECK(Counted::copies == 0 && Counted::comparisons == 0);
    CHECK(changes[0].name == "payload" && changes[0].kind == vardiff::change_kind::modified);
    CHECK(&changes[0].before.template get<Counted>() == before && before->data == 1);
    CHECK(&changes[0].after.template get<Counted>() == after && after->data == 2);
}

} // namespace

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[]{
        {"empty results", empty_results},
        {"flat kinds and mixed counts", all_kinds<vardiff::change>},
        {"deep kinds and mixed counts", all_kinds<vardiff::deep_change>},
        {"flat container records", flat_diff_containers},
        {"keyed moves and leaf edits", keyed_moves_and_edits},
        {"existing options", options_filter_existing_records},
        {"watcher baseline and callbacks", watcher_baseline_and_callbacks},
        {"flat value preservation", no_value_copies_or_comparisons<vardiff::change>},
        {"deep value preservation", no_value_copies_or_comparisons<vardiff::deep_change>},
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

// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

#include <vardiff/vardiff.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>

struct Item { int id; int count; };
template <> struct vardiff::schema<Item> {
    static auto fields() {
        return std::make_tuple(vardiff::field("id", &Item::id),
                               vardiff::field("count", &Item::count));
    }
};

struct Tree { int value = 0; std::vector<Tree> children; };
template <> struct vardiff::schema<Tree> {
    static auto fields() {
        return std::make_tuple(vardiff::field("value", &Tree::value),
                               vardiff::field("children", &Tree::children));
    }
};

template <typename Function>
void measure(std::string_view name, int iterations, Function function) {
    std::uint64_t checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
        checksum += function().size();
    }
    const double elapsed = std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << std::left << std::setw(32) << name << std::right << std::setw(13)
              << std::fixed << std::setprecision(1) << elapsed / iterations
              << " ns/op; checksum=" << checksum << '\n';
}

int main(int argc, char** argv) {
    int iterations = 1000;
    int size = 4096;
    if (argc > 3) {
        std::cerr << "Usage: vardiff_deep_benchmark [iterations] [size]\n";
        return 1;
    }
    for (int argument = 1; argument < argc; ++argument) {
        const std::string_view text = argv[argument];
        auto& output = argument == 1 ? iterations : size;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), output);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || output <= 0) {
            std::cerr << "Expected positive integer arguments\n";
            return 1;
        }
    }
    std::vector<Item> before;
    before.reserve(static_cast<std::size_t>(size));
    std::map<int, Item> ordered;
    std::unordered_map<int, Item> unordered;
    for (int i = 0; i < size; ++i) {
        before.push_back(Item{i, i});
        ordered.emplace(i, Item{i, i});
        unordered.emplace(i, Item{i, i});
    }
    auto rotated = before;
    std::rotate(rotated.begin(), rotated.begin() + 1, rotated.end());
    auto edited = before;
    ++edited.back().count;
    auto reversed = before;
    std::reverse(reversed.begin(), reversed.end());
    auto duplicate = before;
    for (auto& item : duplicate) { item.id %= 8; }
    auto duplicate_rotated = duplicate;
    std::rotate(duplicate_rotated.begin(), duplicate_rotated.begin() + 1, duplicate_rotated.end());
    auto ordered_after = ordered;
    auto unordered_after = unordered;
    ++ordered_after.rbegin()->second.count;
    ++unordered_after.at(size - 1).count;
    const auto key = vardiff::key(&Item::id);
    measure("keyed vector / unchanged", iterations, [&] { return vardiff::deep_diff(before, before, key); });
    measure("keyed vector / leaf edit", iterations, [&] { return vardiff::deep_diff(before, edited, key); });
    measure("keyed vector / rotation", iterations, [&] { return vardiff::deep_diff(before, rotated, key); });
    measure("keyed vector / reversal", iterations, [&] { return vardiff::deep_diff(before, reversed, key); });
    measure("duplicate keys / rotation", iterations, [&] {
        return vardiff::deep_diff(duplicate, duplicate_rotated, key);
    });
    measure("map / leaf edit", iterations, [&] { return vardiff::deep_diff(ordered, ordered_after); });
    measure("unordered_map / leaf edit", iterations, [&] {
        return vardiff::deep_diff(unordered, unordered_after);
    });
    // Equality-only fallback is quadratic: use a smaller explicit workload.
    const auto sample_size = std::min(before.size(), std::size_t{128});
    const std::vector<Item> sample(before.begin(), before.begin() + static_cast<std::ptrdiff_t>(sample_size));
    auto sample_after = sample;
    std::reverse(sample_after.begin(), sample_after.end());
    measure("unkeyed 128 / reversal", iterations, [&] { return vardiff::deep_diff(sample, sample_after); });
    Tree tree;
    auto* leaf = &tree;
    for (int i = 0; i < 64; ++i) {
        leaf->children.push_back(Tree{});
        leaf = &leaf->children[0];
    }
    auto tree_after = tree;
    leaf = &tree_after;
    for (int i = 0; i < 64; ++i) { leaf = &leaf->children[0]; }
    leaf->value = 1;
    vardiff::deep_options indexed;
    indexed.detect_moves = false;
    measure("depth 129 / leaf edit", iterations, [&] { return vardiff::deep_diff(tree, tree_after, indexed); });
}

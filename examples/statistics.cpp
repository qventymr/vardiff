// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

#include <vardiff/vardiff.hpp>

#include <iostream>

int main() {
    struct Inventory { std::vector<int> items; };
    const Inventory before{{1, 2, 3}};
    const Inventory after{{2, 3, 1, 4}};

    // Existing diff() keeps index-based comparison.
    const auto flat = vardiff::summarize(vardiff::diff(
        before, after, vardiff::field("items", &Inventory::items)));

    // deep_diff() detects the rotation as one move, plus one addition.
    const auto changes = vardiff::deep_diff(before.items, after.items);
    const auto stats = vardiff::summarize(changes);
    std::cout << "added=" << stats.added << ", removed=" << stats.removed
              << ", modified=" << stats.modified << ", moved=" << stats.moved
              << ", total=" << stats.total << '\n';
    // added=1, removed=0, modified=0, moved=1, total=2
    return flat.modified == 3 && flat.added == 1 && flat.total == 4 &&
           stats.added == 1 && stats.removed == 0 && stats.modified == 0 &&
           stats.moved == 1 && stats.total == 2 ? 0 : 1;
}

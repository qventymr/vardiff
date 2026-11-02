// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

#include <vardiff/vardiff.hpp>
#include <iostream>

struct Item { int id; int count; };
template <> struct vardiff::schema<Item> {
    static auto fields() {
        return std::make_tuple(vardiff::field("id", &Item::id),
                               vardiff::field("count", &Item::count));
    }
};

struct Inventory {
    std::vector<Item> inventory;
    std::map<std::string, std::optional<Item>> equipment;
};
template <> struct vardiff::schema<Inventory> {
    static auto fields() {
        return std::make_tuple(vardiff::field("inventory", &Inventory::inventory),
                               vardiff::field("equipment", &Inventory::equipment));
    }
};

int main() {
    const Inventory before{{{1, 10}, {2, 20}, {3, 30}}, {{"hand", Item{4, 1}}}};
    const Inventory after{{{2, 20}, {3, 30}, {1, 12}}, {{"hand", Item{4, 2}}}};
    const auto changes = vardiff::deep_diff(before, after, vardiff::key(&Item::id));
    for (const auto& change : changes) {
        if (change.kind == vardiff::change_kind::moved) {
            std::cout << "moved: " << change.from << " -> " << change.name << '\n';
        } else {
            std::cout << change.name << ": " << change.before << " -> " << change.after << '\n';
        }
    }
    // One move plus two leaf edits; inputs remain unchanged.
    return changes.size() == 3 && before.inventory[0].count == 10 ? 0 : 1;
}

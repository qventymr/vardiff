// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

#include <vardiff/vardiff.hpp>

#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

struct Item {
    std::string name;
    double price;
};

template <>
struct vardiff::schema<Item> {
    static auto fields() {
        return std::make_tuple(vardiff::field("name", &Item::name),
                               vardiff::field("price", &Item::price));
    }
};

struct Order {
    std::vector<Item> items{{"book", 10.0}};
    std::optional<std::string> coupon;
    std::array<int, 2> quantities{1, 0};
};

template <>
struct vardiff::schema<Order> {
    static auto fields() {
        return std::make_tuple(vardiff::field("items", &Order::items),
                               vardiff::field("coupon", &Order::coupon),
                               vardiff::field("quantities", &Order::quantities));
    }
};

int main() {
    const Order before{};
    Order after = before;
    after.items[0].price = 12.5;
    after.coupon = "SALE";
    after.quantities[1] = 2;

    for (const auto& change : vardiff::diff(before, after)) {
        std::cout << change.name << ": " << change.before
                  << " -> " << change.after << '\n';
    }

    auto watcher = vardiff::track(after);
    watcher.ignore("items[0].name");
    after.items.push_back(Item{"pen", 2.0});
    watcher.poll();
    if (!watcher.undo() || after.items.size() != 1) {
        return 1;
    }
    if (!watcher.redo() || after.items.size() != 2) {
        return 1;
    }
}

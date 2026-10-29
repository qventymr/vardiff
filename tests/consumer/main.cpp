// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

// Deliberately requests no include path or C++ standard: the package supplies both.
#include <vardiff/vardiff.hpp>

int main() {
    struct Item { int quantity = 1; } item;
    auto watcher = vardiff::track(item, vardiff::field("quantity", &Item::quantity));
    item.quantity = 2;
    const auto changes = watcher.poll();
    if (changes.size() != 1 || changes[0].before.get<int>() != 1 ||
        changes[0].after.get<int>() != 2 || watcher.changed()) {
        return 1;
    }
    return watcher.undo() && item.quantity == 1 ? 0 : 1;
}

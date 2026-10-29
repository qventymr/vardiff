// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

// First include, in a second translation unit: checks self-containment and ODR.
#include <vardiff/vardiff.hpp>
#include <vardiff/vardiff.hpp>

bool header_smoke() {
    struct Item { int count = 1; };
    Item item;
    auto watcher = vardiff::track(item, vardiff::field("count", &Item::count));
    item.count = 2;
    return watcher.changed() && watcher.diff().front().before.get<int>() == 1;
}

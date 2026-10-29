// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

#include <vardiff/vardiff.hpp>

#include <cmath>
#include <iostream>

struct Player {
    int hp = 100;
    double speed = 1.0;
    int render_count = 0;
};

int main() {
    Player player;
    auto watcher = vardiff::track(
        player, vardiff::watch_options{.auto_snapshot = true, .history_limit = 8},
        vardiff::field("hp", &Player::hp),
        vardiff::field("speed", &Player::speed,
            [](double a, double b) { return std::abs(a - b) < 0.01; }),
        vardiff::field("render_count", &Player::render_count));

    watcher.ignore("render_count");
    watcher.on_change([](const auto& changes) {
        for (const auto& change : changes) {
            std::cout << change.name << ": " << change.before
                      << " -> " << change.after << '\n';
        }
    });

    player.hp = 75;
    player.speed = 1.005;
    ++player.render_count;
    watcher.poll(); // The callback runs here; only hp is reported.

    if (watcher.changed() || !watcher.undo() || player.hp != 100) {
        return 1;
    }
    if (!watcher.redo() || player.hp != 75) {
        return 1;
    }

    watcher.set_auto_snapshot(false);
    player.hp = 50;
    watcher.poll();
    if (!watcher.changed()) {
        return 1;
    }
    watcher.snapshot();
}

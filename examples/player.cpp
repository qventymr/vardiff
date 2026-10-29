// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

#include <vardiff/vardiff.hpp>

#include <iostream>

struct Player {
    int hp = 100;
    int coins = 0;
};

int main() {
    Player player;

    auto watcher = vardiff::track(
        player,
        vardiff::field("hp", &Player::hp),
        vardiff::field("coins", &Player::coins)
    );

    player.hp = 75;

    if (watcher.changed()) {
        for (const auto& change : watcher.diff()) {
            std::cout << change.name << ": "
                      << change.before << " -> "
                      << change.after << '\n';
        }
    }

    watcher.snapshot();
}

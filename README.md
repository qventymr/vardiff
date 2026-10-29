# vardiff

**English** | [Русский](README.ru.md)

A small header-only C++20 library for tracking selected object fields.
Compare snapshots, receive deferred change callbacks, inspect container elements,
and restore earlier states. No external runtime dependencies.

## Quick start

Requires a C++20 compiler. CMake builds require CMake 3.20 or newer.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/vardiff_player
```

On multi-configuration generators such as Visual Studio, add `--config Release`
to the build and `-C Release` to CTest; examples are under `build/Release/`.

The example prints `hp: 100 -> 75`.

```cpp
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
```

Full source: [examples/player.cpp](examples/player.cpp).

For a direct compiler invocation:

```sh
mkdir -p build-direct
c++ -std=c++20 -Iinclude examples/player.cpp -o build-direct/player
./build-direct/player
```

## Features

- Explicit field selection with `field()` and `track()`.
- Non-mutating `changed()` and `diff()`; owned copies of snapshot and diff values.
- Deferred callbacks through `on_change()` and `poll()`.
- Path exclusions, custom equality predicates, and configurable automatic snapshots.
- Bounded history with `rollback()`, `undo()`, and `redo()`.
- Copyable custom types and recursive comparison of explicitly registered fields.
- Element-level comparison of `std::vector`, `std::optional`, and `std::array`.
- Comparison of two independent objects.

## Deferred callbacks

Using a fresh `Player player;` and the `Player` type above:

```cpp
auto watcher = vardiff::track(
    player, vardiff::watch_options{.auto_snapshot = true, .history_limit = 32},
    vardiff::field("hp", &Player::hp),
    vardiff::field("coins", &Player::coins));

watcher.ignore("coins");
watcher.on_change([](const auto& changes) {
    for (const auto& change : changes) {
        std::cout << change.name << ": " << change.before
                  << " -> " << change.after << '\n';
    }
});
watcher.on_change("hp", [](const vardiff::change& change) {
    std::cout << "new hp: " << change.after.get<int>() << '\n';
});

player.hp = 75;
watcher.poll();
```

**Callbacks run only during `poll()`.** Assignment, `changed()`, `diff()`,
`snapshot()`, and history restoration do not dispatch callbacks.

By default, a successful `poll()` with changes saves the state observed before
callbacks. Callback edits remain pending for the next poll. Disable automatic
updates with `watcher.set_auto_snapshot(false)`; then differences are reported
again until `snapshot()` is called.

See [examples/events.cpp](examples/events.cpp) for a complete example, including
a custom equality predicate.

## Nested structures and containers

Register fields before first use; registered types do not need `operator==`:

```cpp
struct Item {
    std::string name;
    double price;
};

template <>
struct vardiff::schema<Item> {
    static auto fields() {
        return std::make_tuple(
            vardiff::field("name", &Item::name),
            vardiff::field("price", &Item::price));
    }
};
```

Fields containing `Item`, `std::vector<Item>`, `std::optional<Item>`, or
`std::array<Item, N>` are compared recursively. Value trees with
`std::vector<Node>` children are supported. A registered root object can be
tracked with `vardiff::track(object)`.

Change names are paths such as `items[2].price`. Containers are compared by
index. Added or removed elements use `change_kind::added` / `removed`;
the absent side has `value.has_value() == false` and prints `<missing>`.

Use `watcher.ignore("items[1].price")` to exclude one leaf, or
`watcher.ignore("items")` to exclude a whole branch.

Complete example: [examples/nested.cpp](examples/nested.cpp).

## History and typed values

```cpp
watcher.undo();                    // Previous saved state; false at the boundary.
watcher.redo();                    // Next saved state.
watcher.rollback();                // Same as undo().
watcher.rollback(0);               // A chosen retained snapshot.
watcher.set_history_limit(16);     // Includes the current snapshot.

const auto states = watcher.history(); // Independent copies of value tuples.
const auto count = watcher.history_size();
const auto position = watcher.history_position();
```

The initial state is retained. The default limit is 64 snapshots.
A new snapshot after undo discards the redo branch. Restoration discards unsaved
edits and requires assignable fields.

`before` and `after` preserve their types: use `change.before.get<int>()`,
`change.after.get<std::string>()`, or `get<MyType>()`. Wrong-type access throws
`std::bad_variant_access`. Stream output uses the stored type's `operator<<`
when available.

Custom comparison is the third argument of `field()`: its predicate returns
`true` for equal values. It overrides the entire field's comparison, producing
one atomic change. Comparators must be pure and callable as const.

## Compare independent objects

```cpp
auto changes = vardiff::diff(before, after,
    vardiff::field("hp", &Player::hp),
    vardiff::field("coins", &Player::coins));
bool different = vardiff::changed(before, after, vardiff::field("hp", &Player::hp));
```

For registered types, use `vardiff::diff(before, after)` and
`vardiff::changed(before, after)`.

## CMake integration

Vendor the source and use:

```cmake
add_subdirectory(path/to/vardiff)
target_link_libraries(my_app PRIVATE vardiff::vardiff)
```

Or install a package:

```sh
cmake --install build --prefix ./install
```

Then consume it with:

```cmake
find_package(vardiff 0.2 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE vardiff::vardiff)
```

Pass the install prefix to the consumer's CMake configure command using
`-DCMAKE_PREFIX_PATH=/path/to/install`. The installed package is relocatable.

| Option | Standalone default | As a subdirectory |
| --- | --- | --- |
| `VARDIFF_BUILD_TESTS` | ON | OFF |
| `VARDIFF_BUILD_EXAMPLES` | ON | OFF |
| `VARDIFF_BUILD_BENCHMARKS` | OFF | OFF |
| `VARDIFF_WARNINGS_AS_ERRORS` | OFF | OFF |

Warning options apply only to vardiff's own executables, not to consumers.

## Contracts and scope

The watched object is borrowed and must remain alive at the same address.
Snapshots use the normal copy semantics of each field's type.
Only explicitly registered fields are traversed. The default floating-point
comparison is exact `operator==`, so NaN remains different even after a snapshot.

There is no background observation, automatic reflection, pointer-graph traversal,
JSON, or thread synchronization.

## Documentation and development

- [API contracts and exception behavior](docs/API.md)
- [Performance notes and benchmark](docs/performance.md)
- [Contributing](CONTRIBUTING.md)
- [Changelog](CHANGELOG.md)
- [Release procedure](docs/RELEASING.md)

The test executables cover 50 scenarios; CTest also runs three examples.
The [CI workflow](.github/workflows/ci.yml) is configured for GCC, Clang,
Apple Clang, MSVC, sanitizers, and an installed-package consumer.

## License

Copyright 2026 Dmitry. Licensed under [Apache-2.0](LICENSE).
Attribution notices are in [NOTICE](NOTICE). Include both files when redistributing
the library.

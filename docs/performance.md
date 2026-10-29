# Performance

[Русский](performance.ru.md)

## Implemented paths

- `changed()` stops at the first difference without creating diff values or
  copying watched fields. Without ignored paths, it does not build path strings.
- An unchanged `poll()` returns after the check without capturing a new state.
- `diff()` skips equal subtrees before creating child paths. Equal container
  elements also skip index-path construction when no exclusions are active.
- Snapshot capture copies each selected top-level field exactly once directly
  into its tuple, without an intermediate tuple.
- Bounded history evicts old states without moving other saved values.
  Watcher copying and history branching share immutable saved storage.
- Callback dispatch does not copy std::function objects. New registrations are
  deferred, while mutable callback state persists.
- Nested schema descriptors are cached on first use.

First schema initialization can allocate. Custom comparisons, copying, and output
operators can also allocate or perform expensive work.

Tests count copies to verify copy-free unchanged checks, short-circuit behavior,
and one copy per selected field when capturing a snapshot.

## Cost model

For flat fields, `changed()` visits at most the selected fields. Recursive
comparison can visit every registered leaf and container element. An equal
vector needs a linear scan; a size difference can be detected immediately.

A diff must create all events and copy their values. Equal-subtree checks can
cause repeated comparisons when a subtree differs. The tradeoff is avoiding
path construction for equal subtrees; the benefit depends on shape and workload.

A changed poll additionally copies all selected fields into a consistent
pre-callback state. Even a one-element vector edit requires copying the selected
vector for the snapshot. Choose fields and the history limit accordingly.

The limit bounds the number of snapshots, not bytes. Memory depends on field
sizes and their normal copy semantics. `history()` intentionally copies retained
snapshots. For UI state, prefer `history_size()`, `history_position()`,
`can_undo()`, and `can_redo()` when copies are unnecessary.

## Standalone benchmark

No benchmark framework is required:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DVARDIFF_BUILD_BENCHMARKS=ON
cmake --build build-bench --target vardiff_benchmark
./build-bench/vardiff_benchmark 10000
```

The object contains an integer and a vector of 4096 integers. Workloads exercise
unchanged checks, a first-field difference, a last-element diff, and changed polls
with history. Output includes ns/op and result checksums.

This is a diagnostic benchmark. Compiler optimization, CPU, and system load affect
results. CI uses deterministic behavior/copy-count checks, not timing thresholds.

## Recorded optimization check

One local run used GCC 15.2, C++20, Release (-O3, NDEBUG), 10,000 iterations,
and a vector of 4096 integers. Both versions were 0.2 implementations; the comparison
was before/after adding equal-subtree skipping, not against the original MVP.

| Workload | Before, ns/op | After, ns/op |
| --- | ---: | ---: |
| Unchanged diff | 115655.1 | 1071.9 |
| Last-element diff | 114901.3 | 7264.2 |
| Changed integer poll | 109009.3 | 1535.3 |

Checksums matched. The main saving was avoiding path construction for thousands
of equal items. These numbers describe that workload and are not a performance
guarantee for other types, shapes, or platforms.

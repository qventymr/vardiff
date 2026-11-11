# Changelog

## 0.2.0 — Unreleased

### Added

- Diff Statistics: `summarize()` returns added, removed, modified, moved, and total
  record counts for existing `diff()` and `deep_diff()` results without copying
  values or changing comparison behavior; includes tests, an example, and
  bilingual documentation.
- Deep Diff Engine: `deep_diff()`, identity selectors through `key()`, minimum
  sequence move sets, FIFO/strict duplicate-key policies and bounded recursion.
- Recursive `std::map`/`std::unordered_map` support shared by deep diffs,
  existing comparisons, watchers and snapshot restoration.
- Owned `deep_change` results with source/destination move paths and indices,
  escaped map-key paths, bilingual contracts, examples and deep benchmarks.
- Deep diff regression/copy-count tests, exhaustive permutation checks and
  randomized keyed edit validation.
- Deferred callbacks through `on_change()` and `poll()`, path exclusions,
  custom equality predicates, and configurable automatic snapshots.
- Bounded snapshot history, `rollback()`, `undo()`, and `redo()`.
- Copyable custom types, explicit field schemas, nested value trees,
  and independent-object comparison.
- Element-level diffs for `std::vector`, `std::optional`, and `std::array`.
- Copy-free idle checks, early exits, unchanged-subtree skipping, and
  a standalone benchmark.
- CMake install/export support and a relocatable `find_package(vardiff)` package.

### Fixed

- MSVC builds with strict warnings and history retrieval for copy-only field types
  with explicitly deleted move constructors.

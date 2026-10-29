# Changelog

## 0.2.0 — Unreleased

### Added

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

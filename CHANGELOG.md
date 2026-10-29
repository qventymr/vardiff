# Changelog

## 0.2.0 — Unreleased

Prepared for the first public release.

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
- GitHub Actions configuration, package-consumer smoke test, contribution
  templates, Apache-2.0 licensing, and English/Russian READMEs.

### Compatibility

The original `field()`, `track()`, `changed()`, `diff()`, and `snapshot()` API
remains available. Version 0.x APIs may evolve; changes should be documented here.

## 0.1.0 — Local MVP

- Header-only C++20 tracking of selected `int`, `float`, `double`, `bool`, and
  `std::string` fields.
- Value snapshots and non-mutating `changed()` / `diff()` checks.
- CMake build, tests, and the Player example.

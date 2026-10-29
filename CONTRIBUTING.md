# Contributing

Bug reports and focused pull requests are welcome. Please include a minimal
reproduction and the compiler, OS, and vardiff version you used.

## Local development

Use a C++20 compiler and CMake 3.20 or newer. Tests and benchmarks use only
the standard library.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DVARDIFF_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Repeat with `-DCMAKE_BUILD_TYPE=Release` in a separate build directory when
changing public behavior. For multi-configuration generators, pass
`--config Debug` to the build and `-C Debug` to CTest.

For the optional benchmark:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DVARDIFF_BUILD_BENCHMARKS=ON
cmake --build build-bench --target vardiff_benchmark
./build-bench/vardiff_benchmark 10000
```

## Change expectations

- Keep the library header-only, C++20, and free of external runtime dependencies.
- Add a regression test for a bug fix or a changed behavior. Tests must also run
  in Release; do not use disabled `assert` checks as the test mechanism.
- Document API changes and snapshot/callback semantics. Keep the English and
  Russian READMEs consistent.
- Preserve the Apache-2.0 notices. Contributions are provided under the same
  license as the project; see [LICENSE](LICENSE).

The CI workflow is configured for GCC, Clang, Apple Clang, and MSVC in Debug
and Release, plus Linux sanitizers. Each compiler job also installs vardiff
and builds a separate `find_package` consumer. The workflow results on GitHub
are the source of truth for platform coverage.

Release preparation is described in [docs/RELEASING.md](docs/RELEASING.md).

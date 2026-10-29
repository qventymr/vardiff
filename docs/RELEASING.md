# Publishing vardiff

The source tree is prepared for version 0.2.0. Creating a GitHub repository,
pushing commits, creating tags, and publishing releases are separate steps.

## First upload

1. Create an empty GitHub repository named vardiff under your account or organization.
   Leave GitHub's README, license, and gitignore initialization unchecked because
   those files already exist locally.
2. Review the local commit and author information:

   ```sh
   git status --short
   git log -1 --format=fuller
   ```

3. Add the actual repository URL and push the existing main branch:

   ```sh
   git remote add origin <your-repository-url>
   git push -u origin main
   ```

The URL above is a placeholder for the repository you create. If origin already
exists, inspect it with `git remote -v` instead of adding it again.

Review the commit author before publishing. GitHub-provided noreply addresses
are account-specific; copy the full address from your account's email settings
when configuring attribution. Use repository-local Git configuration if the
project identity should differ from your global Git settings.

The CI workflow is configured to run on pushes, pull requests, and manual dispatch.
Its full checkout commit pin was verified against the official
[checkout v7.0.1 release](https://github.com/actions/checkout/releases/tag/v7.0.1).
The workflow uses read-only repository permissions and disables persisted checkout
credentials, following the relevant
[GitHub Actions guidance](https://docs.github.com/en/actions/reference/security/secure-use).

## Validate a release candidate

Run from a clean checkout:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DVARDIFF_WARNINGS_AS_ERRORS=ON -DVARDIFF_BUILD_BENCHMARKS=ON
cmake --build build-release --parallel 2
ctest --test-dir build-release --output-on-failure --no-tests=error
cmake --install build-release --prefix ./stage

cmake -S tests/consumer -B build-consumer -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/stage"
cmake --build build-consumer --parallel 2
ctest --test-dir build-consumer --output-on-failure --no-tests=error
```

For Visual Studio, also use `--config Release` for build/install commands and
`-C Release` for CTest. Set CMAKE_PREFIX_PATH to the absolute stage path.

Review the GCC, Clang, Apple Clang, MSVC, and sanitizer jobs on GitHub before
tagging a release. A local Linux build does not establish Windows/macOS coverage.

## Version and release notes

- Keep the project version in CMakeLists.txt, the changelog heading, and release
  notes consistent.
- For a minor-version change, update the consumer version requirement and both
  READMEs' find_package examples.
- Replace the Unreleased heading date in CHANGELOG.md with the release date.
- Review [the prepared 0.2.0 notes](releases/v0.2.0.md).
- Keep LICENSE and NOTICE in distributed source and installed packages.

Once the release commit has passed CI:

```sh
git tag -a v0.2.0 -m "vardiff 0.2.0"
git push origin main
git push origin v0.2.0
```

Create a GitHub Release for v0.2.0 and use docs/releases/v0.2.0.md as its body.
No workflow in this repository automatically publishes a release.

## Source archives

Generate archives from a reviewed commit or tag, not from the working directory:

```sh
mkdir -p dist
git archive --format=zip --prefix=vardiff-0.2.0/ --output=dist/vardiff-0.2.0.zip v0.2.0
git archive --format=tar.gz --prefix=vardiff-0.2.0/ --output=dist/vardiff-0.2.0.tar.gz v0.2.0
```

These archives include tracked source, tests, docs, license, and notices.
Ignored build trees, personal editor files, and local Git configuration are excluded.
The dist directory is ignored by Git.

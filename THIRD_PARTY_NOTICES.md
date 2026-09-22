# Third-party notices

This file records third-party material that this repository uses or may
incorporate. It is separate from the project's Apache License 2.0 and does
not relicense third-party material.

Other dependencies must be added here before they are committed, with their
source, exact version or commit, license, and required notices.

## doctest

- **Source:** <https://github.com/doctest/doctest>
- **Exact version/commit:** `v2.5.0`, `d44d4f6e66232d716af82f00a063759e9d0e50d6`
- **License:** MIT, upstream [LICENSE.txt](https://github.com/doctest/doctest/blob/v2.5.0/LICENSE.txt)
- **Role/status:** CMake FetchContent host-test dependency. v2.5.0 is pinned because the prior v2.4.11 CMake metadata used a pre-3.5 minimum rejected by CMake 4; the exact commit is pinned in `tests/host/CMakeLists.txt`.

## ESP-IDF

- **Source:** <https://github.com/espressif/esp-idf/tree/v5.5.4>
- **Exact version:** `v5.5.4`
- **License:** Apache-2.0, upstream [LICENSE](https://github.com/espressif/esp-idf/blob/v5.5.4/LICENSE)
- **Role/status:** required firmware toolchain and manifest dependency; no ESP-IDF source is copied into this repository. The upstream notices remain authoritative.

## GitHub Actions checkout

- **Source:** <https://github.com/actions/checkout>
- **Exact version/commit:** `v4.2.2`, `11bd71901bbe5b1630ceea73d27597364c9af683`
- **License:** MIT, upstream [LICENSE](https://github.com/actions/checkout/blob/v4.2.2/LICENSE)
- **Role/status:** pinned CI checkout action in `.github/workflows/ci.yml`.

No third-party source files, vehicle captures, credentials, or private data are
included by this core.

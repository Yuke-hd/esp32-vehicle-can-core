# ESP32 vehicle CAN core scaffold

This repository contains the portable vehicle CAN core, receive-only ESP-IDF
transport, and isolated T-CAN485 bench-ACK firmware. It is intentionally not a
vehicle product repository: make/model decoders, telemetry services, lighting
policy, board support, DBC files, and vehicle captures belong in a consuming
controller repository.

The package identity and canonical GitHub repository are
`esp32-vehicle-can-core` and
`https://github.com/Yuke-hd/esp32-vehicle-can-core`. Downstream consumers pin
the generic components with host FetchContent or the ESP-IDF Component Manager;
see
[`consumer-integration.md`](consumer-integration.md).

## Pinned toolchains

- ESP32 firmware: ESP-IDF v5.5.4, target esp32.
- Host library and tests: C++17, CMake 3.20 minimum (CMake 4.4 modern
  compatibility leg), clang-format major version 14, doctest tag v2.5.0 at
  commit d44d4f6e66232d716af82f00a063759e9d0e50d6 (MIT license).

The ESP-IDF manifest requires exactly 5.5.4; it does not download an
unrelated SDK at configure time. The host test dependency is fetched by CMake
using the exact doctest commit above.

## Reproducible commands

From the repository root:

    python3 tools/check_toolchain.py --scope host
    cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
    cmake --build build/host --parallel
    ctest --test-dir build/host --output-on-failure

For the retained isolated bench firmware, activate ESP-IDF v5.5.4, verify the
firmware scope, and build `firmware/tcan485-bench-ack-only` with `idf.py`.

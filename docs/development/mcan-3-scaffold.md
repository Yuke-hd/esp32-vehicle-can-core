# MCAN-3 scaffold

This repository contains the portable Mazda CAN telemetry decoder, service
contracts, receive-only CAN primitives, and the isolated T-CAN485 bench
firmware. The WeAct/accessory-controller firmware moved to the dedicated
[mazda-can-accessory-controller repository](https://github.com/Yuke-hd/mazda-can-accessory-controller);
this repository is not the owner of that hardware-specific application.

The scaffold does not decode private captures, export telemetry, or provide a
vehicle-side CAN data-generation path. Raw vehicle captures remain private
analysis data.

## Pinned toolchains

- ESP32 firmware: ESP-IDF v5.5.4, target esp32.
- Host library and tests: C++17, CMake 3.20 minimum (CMake 4.4 modern
  compatibility leg), clang-format major version 14, doctest tag v2.5.0 at
  commit d44d4f6e66232d716af82f00a063759e9d0e50d6 (MIT license).

The ESP-IDF manifest requires exactly 5.5.4; it does not download an
unrelated SDK at configure time. The host test dependency is fetched by CMake
using the exact doctest commit above.

## Reproducible commands

From the repository root, run the following commands:

    python3 tools/check_toolchain.py --scope host
    cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
    cmake --build build/host --parallel
    ctest --test-dir build/host --output-on-failure

For the retained isolated bench firmware, activate ESP-IDF v5.5.4, verify
the firmware scope, and run:

    python3 tools/check_toolchain.py --scope firmware
    cd firmware/tcan485-bench-ack-only
    idf.py set-target esp32
    idf.py build

## Safety and scope

The shared can_bus component is receive-only. The
tcan485-bench-ack-only project is separately named, uses the
bench_can_ack normal-mode binding only for isolated classic-CAN
acknowledgement, and must never be connected to a vehicle. No product or
vehicle firmware is built by this repository.

The accessory repository owns WeAct board pin records, vehicle listen-only
composition, concrete lighting policy/renderer components, and their hardware
validation. Keep those changes and releases in that repository.

Dependency source, exact version/commit, role, and upstream license links are
recorded in THIRD_PARTY_NOTICES.md. Privacy, license, attribution,
receive-only, and bench-isolation rules remain authoritative in
CONTRIBUTING.md.

# ESP32 Vehicle CAN Core

Make-agnostic C++17 vehicle CAN primitives with ESP32/ESP-IDF transport,
bounded queues, health/freshness contracts, host tests, and an isolated
T-CAN485 bench-ACK firmware target. The intended repository name and package
identity are `esp32-vehicle-can-core`; the canonical GitHub repository is
`https://github.com/Yuke-hd/esp32-vehicle-can-core`.

This repository is the reusable foundation for downstream vehicle projects.
It deliberately does not contain a make/model decoder, product publication
service, lighting policy, board product, DBC, or vehicle capture. Those belong
in the consuming controller repository. Host consumers use CMake FetchContent
and ESP-IDF consumers use a pinned Component Manager Git dependency; see
[`docs/development/consumer-integration.md`](docs/development/consumer-integration.md).

## Contents and ownership

- `components/vehicle_core`: portable frame, time, signal, reading, notification,
  health, and decoder-contract types. It has no ESP-IDF, RTOS, transport, or
  make/model dependency.
- `components/vehicle_telemetry`: make-agnostic runtime that composes injected
  acquisition, frame-processing, and observer strategies. It owns lifecycle,
  timeout, and transport diagnostics, not product semantics.
- `components/can_bus`: ESP-IDF/TWAI receive-only transport with bounded
  buffering and diagnostics.
- `components/bench_can_ack`: the explicitly isolated normal-mode CAN ACK
  adapter. It is not a vehicle or product transport.
- `firmware/tcan485-bench-ack-only`: separately named ESP32 bench firmware
  that may acknowledge classic-CAN traffic on a protected bench only.

## Build and test

Required host tools are Bash, Git, Python 3.8+, ripgrep, a C++17 compiler,
CMake 3.20+, Ninja, and clang-format 14. ESP-IDF v5.5.4 and its ESP32
toolchain apply only to the isolated bench firmware:

    python3 tools/check_toolchain.py --scope host
    cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
    cmake --build build/host --parallel
    ctest --test-dir build/host --output-on-failure

For the bench project:

    python3 tools/check_toolchain.py --scope firmware
    cd firmware/tcan485-bench-ack-only
    idf.py set-target esp32
    idf.py build

The bench project uses TWAI normal mode only to ACK compliant classic-CAN
frames on an isolated, protected bench. It has no public data-frame transmit
API and must never be connected to a vehicle. See
[`docs/development/mcan-13-bench-ack-only.md`](docs/development/mcan-13-bench-ack-only.md)
for the physical isolation boundary.

## Safety boundary

- Shared CAN acquisition is receive-only and has no business-level transmit
  API.
- Active transmission is permitted only in the explicitly named isolated
  bench target.
- This project is not a replacement for a factory instrument cluster and
  must not be used for safety-critical decisions.
- Wiring, termination, power, and fail-silent behavior require appropriate
  bench validation before any vehicle connection.

## License and data policy

Project-authored source, documentation, tests, and tooling are licensed under
the [Apache License 2.0](LICENSE). This core contains no vehicle signal
definitions or private captures. If a downstream project adds vehicle data,
it must apply its own provenance and privacy review; the generic policy in
[`docs/policies/license-and-vehicle-data.md`](docs/policies/license-and-vehicle-data.md)
remains a useful baseline.

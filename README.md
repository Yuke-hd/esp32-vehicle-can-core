# ESP32 Vehicle CAN Core

Reusable ESP-IDF building blocks for reading a vehicle's CAN bus on an ESP32
and turning raw frames into telemetry, without tying you to any particular
car.

> **Receive-only by design.** The vehicle-facing code listens to the bus and
> never transmits. Don't use this project for safety-critical decisions or as a
> replacement for a factory instrument cluster.

## What it does

You bring the vehicle knowledge. This repo provides everything else:

```text
  CAN bus ──► can_bus ──► vehicle_telemetry ──► your decoder ──► your outputs
             (receive)    (runtime loop)        (make/model)     (display, logging, ...)
                  └──────── all built on vehicle_core types ────────┘
```

| This repo provides | Your project provides |
| --- | --- |
| Receive-only CAN driver for the ESP32 | Make/model decoding and signal definitions (DBC, IDs) |
| A runtime that moves frames from the bus to your code | What to do with decoded values: displays, logging, lighting |
| Shared types for frames, signals, readings, and health | Board support and the firmware app |
| Host builds and tests that run without hardware | Vehicle captures and protocol evidence |

## Components

Add only the components you need:

| Component | Use it when you want to... |
| --- | --- |
| [`vehicle_core`](components/vehicle_core) | Share frame, signal, and reading types. Plain C++17 with no ESP-IDF dependency, so it also builds on your PC. **Always required.** |
| [`can_bus`](components/can_bus) | Receive CAN frames on an ESP32 (TWAI). |
| [`vehicle_telemetry`](components/vehicle_telemetry) | Use a ready-made loop that reads frames, hands them to your decoder, and notifies your observers. Requires `can_bus`. |
| [`bench_can_ack`](components/bench_can_ack) | Test on an isolated bench only. **Never connect it to a vehicle.** |

## Quick start: use it in your project

Add the components to your ESP-IDF project's `main/idf_component.yml`. Pin
every entry to the same commit SHA:

```yaml
dependencies:
  vehicle_core:
    git: https://github.com/Yuke-hd/esp32-vehicle-can-core.git
    path: components/vehicle_core
    version: "<commit-sha>"
  can_bus:
    git: https://github.com/Yuke-hd/esp32-vehicle-can-core.git
    path: components/can_bus
    version: "<commit-sha>"
  # Optional: the runtime loop
  vehicle_telemetry:
    git: https://github.com/Yuke-hd/esp32-vehicle-can-core.git
    path: components/vehicle_telemetry
    version: "<commit-sha>"
```

Next, follow the [consumer integration guide](docs/guides/consumer-integration.md).
It covers a minimal receive-and-print app, host CMake (FetchContent) setup, and
updating the pinned commit.

## Quick start: build and test this repo

You need CMake, Ninja, and a C++17 compiler. You don't need any hardware.

```sh
python3 tools/check_toolchain.py --scope host   # check your toolchain
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

For supported versions and sanitizer builds, see
[host build guide](docs/guides/host-build.md). To build the
ESP32 bench firmware under [`firmware/`](firmware), you also need
ESP-IDF v5.5.4. See [bench firmware](docs/guides/bench-firmware.md).

For the generated Arduino-ESP32 package and its exact supported configuration,
see [Arduino package](docs/guides/arduino-package.md).

## Safety

- The vehicle-facing transport is **receive-only** and has no transmit API.
- Only the isolated bench firmware ever acknowledges frames, and it must stay
  disconnected from any vehicle harness.
- Validate wiring, termination, and power on a bench before connecting to a
  vehicle.

Details: [receive-only acquisition](docs/design/receive-only-acquisition.md).

## Documentation

| Topic | Doc |
| --- | --- |
| Integrating into your project | [Consumer integration](docs/guides/consumer-integration.md) |
| How the components fit together | [Module boundaries](docs/design/module-boundaries.md) |
| Runtime tuning and work budgets | [Runtime work budgets](docs/design/runtime-work-budgets.md) |
| Vehicle data and capture rules | [License and vehicle-data policy](docs/policies/license-and-vehicle-data.md) |

See the [documentation index](docs/README.md) for everything else.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Don't commit raw vehicle captures,
VINs, credentials, precise locations, or non-anonymized trip data.

## License

[Apache License 2.0](LICENSE). Third-party notices are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

# ESP32 Vehicle CAN Core

ESP32/ESP-IDF components for vehicle CAN acquisition and telemetry with portable host builds and tests. 

This repository is a reusable foundation for downstream vehicle projects. It
does not include make/model decoding, product publication, lighting policy,
board products, DBC files, or vehicle captures. Those belong in the consuming
controller repository.

## Contents

- [About the project](#about-the-project)
- [Components](#components)
- [Getting started](#getting-started)
  - [Host build and tests](#host-build-and-tests)
  - [Isolated bench firmware](#isolated-bench-firmware)
- [Usage](#usage)
  - [ESP-IDF consumer](#esp-idf-consumer)
- [Safety boundary](#safety-boundary)
- [Contributing](#contributing)
- [License and third-party notices](#license-and-third-party-notices)

## About the project

The project separates portable CAN and telemetry contracts from ESP-IDF
transport code. ESP-IDF controller projects can add the components they need
through the Component Manager.

## Components

| Component | Purpose |
| --- | --- |
| `vehicle_core` | Portable frame, time, signal, reading, notification, health, and decoder-contract types. It has no ESP-IDF, RTOS, transport, or make/model dependency. |
| `vehicle_telemetry` | Generic runtime that composes injected acquisition, frame-processing, and observer strategies, and owns lifecycle and transport diagnostics. |
| `can_bus` | ESP-IDF/TWAI receive-only transport with bounded buffering and diagnostics. |
| `bench_can_ack` | Isolated adapter for acknowledging compliant classic-CAN frames on a protected bench. It is not a vehicle transport. |

## Getting started

The host build uses CMake and Ninja and requires a C++17 compiler. The
repository's host toolchain check also requires Bash, Git, Python 3.8 or newer,
`clang-format-14`, and ripgrep. Run the check before configuring:

```sh
python3 tools/check_toolchain.py --scope host
```

### Host build and tests

From the repository root:

```sh
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

On a clean build, CMake downloads the pinned doctest dependency during
configuration, so network access is needed unless that source is already in
the CMake FetchContent cache. See [supported host builds](docs/development/supported-build.md)
for the supported CMake versions and optional sanitizer build.

### Isolated bench firmware

The separate `tcan485-bench-ack-only` project requires ESP-IDF v5.5.4 and its
ESP32 toolchain. Activate the ESP-IDF environment, then run:

```sh
python3 tools/check_toolchain.py --scope firmware
cd firmware/tcan485-bench-ack-only
idf.py set-target esp32
idf.py build
```

This firmware uses TWAI normal mode only to acknowledge compliant classic-CAN
frames on an isolated, protected bench. It must never be connected to a
vehicle. See the [bench target instructions](docs/development/mcan-13-bench-ack-only.md)
for the physical isolation requirements.

## Usage

In an ESP-IDF downstream controller project, add the core components you need
through the Component Manager and pin them to a reviewed commit. The example
below declares `vehicle_core` and shows how to add optional runtime and
transport components. See the
[consumer integration guide](docs/development/consumer-integration.md) for
dependency direction, updates, and ownership.

The consuming controller owns its make/model decoder, signal definitions,
publication contracts, board support, and firmware. This core keeps those
project-specific behaviors outside its generic components.

### ESP-IDF consumer

In the downstream project's `main/idf_component.yml` (or the manifest for the
component that uses the core), declare `vehicle_core` as a Git dependency.
Replace the placeholder with a reviewed full commit SHA:

```yaml
dependencies:
  vehicle_core:
    git: https://github.com/Yuke-hd/esp32-vehicle-can-core.git
    path: components/vehicle_core
    version: "<reviewed-core-commit>"
```

If the controller uses `vehicle_telemetry`, also declare `can_bus`: the runtime
requires it for its ESP-IDF TWAI adapter. The `can_bus` component can also be
used without `vehicle_telemetry`. Add the needed entries as siblings of
`vehicle_core` under the same `dependencies:` key, using the same reviewed
commit for each:

```yaml
  vehicle_telemetry:
    git: https://github.com/Yuke-hd/esp32-vehicle-can-core.git
    path: components/vehicle_telemetry
    version: "<reviewed-core-commit>"
  can_bus:
    git: https://github.com/Yuke-hd/esp32-vehicle-can-core.git
    path: components/can_bus
    version: "<reviewed-core-commit>"
```

Declare only the components the controller uses. The runtime accepts injected
acquisition, processing, and observer strategies; the controller implements
its own vehicle-specific behavior.

## Safety boundary

- Shared CAN acquisition is receive-only and exposes no business-level
  transmit API.
- Active CAN acknowledgement is limited to the explicitly named isolated
  bench target.
- The bench target must remain disconnected from any vehicle harness.
- This project is not a replacement for a factory instrument cluster and
  must not be used for safety-critical decisions.
- Wiring, termination, power, and fail-silent behavior require appropriate
  bench validation.

See [receive-only acquisition](docs/development/mcan-7-listen-only-acquisition.md)
for transport behavior and [license and vehicle-data policy](docs/policies/license-and-vehicle-data.md)
for restrictions on captures and other vehicle data.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the contribution workflow, safety
requirements, and vehicle-data rules. In particular, raw vehicle captures,
VINs, credentials, precise locations, and non-anonymized trip data must not be
committed or attached to public project artifacts.

## License and third-party notices

Project-authored source, documentation, tests, and tooling are licensed under
the [Apache License 2.0](LICENSE). Third-party dependencies and their licenses
are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

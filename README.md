# Mazda CAN Telemetry

Receive-only Mazda CX-5 KF CAN telemetry libraries, service contracts, and
host tests. The source repository also retains the isolated
T-CAN485 bench-ACK firmware. The WeAct/accessory-controller product moved to
the [dedicated accessory-controller repository](https://github.com/Yuke-hd/mazda-can-accessory-controller),
which is now the intended home for its board, vehicle firmware, concrete
lighting implementation, and hardware validation.

The current validation vehicle is an Australian-market 2019 Mazda CX-5 Akera
with the 2.5T engine, six-speed automatic transmission, AWD, and MRCC.
Third-party DBC definitions are candidate leads only; reviewed
capture-derived definitions for Issue #51 remain in
[docs/protocol/mazda_custom.dbc](docs/protocol/mazda_custom.dbc) with their
provenance and vehicle-data review.

## Build and test

The portable C++17 decoder, Mazda façade, receive-only CAN contracts, and host
tests are configured at the repository root. See the
[MCAN-3 scaffold](docs/development/mcan-3-scaffold.md) and
[supported-build guide](docs/development/supported-build.md) for pinned
tool versions and reproducible commands.

Required host tools are Bash, Git, Python 3.8+, ripgrep, a C++17 compiler,
CMake 3.20+, Ninja, and clang-format 14. ESP-IDF v5.5.4 and its ESP32
toolchain apply only to the retained isolated bench firmware:

    python3 tools/check_toolchain.py --scope host
    cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
    cmake --build build/host --parallel
    ctest --test-dir build/host --output-on-failure

For the bench project:

    python3 tools/check_toolchain.py --scope firmware
    cd firmware/tcan485-bench-ack-only
    idf.py set-target esp32
    idf.py build

The bench project is a separately named LILYGO/TTGO isolated receiver that
uses normal mode only to ACK classic-CAN traffic. It has no public
frame-transmission API and must never be connected to a vehicle. See
[MCAN-13](docs/development/mcan-13-bench-ack-only.md) for its physical
isolation boundary.

## Safety boundary

- Shared CAN acquisition is receive-only and has no business-level transmit API.
- Active transmission is permitted only in the prominently marked isolated
  bench target.
- This project is not a replacement for the factory instrument cluster and
  must not be used for safety-critical decisions.
- Wiring, termination, power, and fail-silent behavior require appropriate
  bench validation before any vehicle connection.

SavvyCAN is an external tool for private analysis and isolated-bench work; it
is not a repository-owned product and does not authorize vehicle-side CAN
transmission.

## License and data policy

Project-authored source, documentation, tests, and tooling are licensed under
the [Apache License 2.0](LICENSE). Candidate signal material from
[comma.ai/opendbc](https://github.com/commaai/opendbc) remains under its MIT
license and is attributed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Raw vehicle captures are private analysis data and must not be committed,
attached to Issues, or shared externally. Only a reviewed anonymized fixture
may be published, and it must contain no VIN, credentials, precise location,
absolute timestamp, or reconstructable trip pattern. See the
[license and vehicle-data policy](docs/policies/license-and-vehicle-data.md)
and [CONTRIBUTING.md](CONTRIBUTING.md).

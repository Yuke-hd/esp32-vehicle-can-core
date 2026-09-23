# S2-C architecture contract checks

The host gate consolidates architecture validation without making unfinished
hardware work a prerequisite. It is registered once as the
`architecture_contracts` CTest and checks only the reusable core and isolated
bench boundary.

## Compiled boundaries

The gate configures and builds a temporary consumer against only
`components/vehicle_core`. The consumer links `vehicle_core`, exercises its portable
frame-validity function, and checks value-copy reading/notification types. Its
compile command and dependency output, together with `vehicle_core` translation
units, are inspected for controller, ESP-IDF, RTOS, CAN-driver, and other
component inputs.

The gate also configures, builds, and runs the project-owned adapter tests in
components/bench_can_ack/tests. These compile the real isolated-bench binding
translation unit and assert its normal mode, fixed pins, and disabled
data-frame queue. The T-CAN485 firmware project remains separately composed.
Make/model and product adapter checks are owned by the downstream controller
repository that consumes this core.

## Retired capture check

The same gate scans active code, tests, build files, and workflows for the
retired raw_capture product markers. Historical protocol/development notes are
not active dependencies. No source DBC or private capture data is read or
published by this check.

## Verification

Run the consolidated check from the repository root:

    python3 tools/check_architecture.py --root . --compiler c++ --cmake cmake

The deterministic negative fixtures for a private vehicle_core dependency and
a retired marker in active build files can be run directly:

    python3 tests/tools/check_architecture_test.py -v

The normal host suite runs these checks through CTest alongside the public
header boundary and portable contract tests. Firmware, isolated-bench
hardware, and vehicle validation remain outside this software gate.

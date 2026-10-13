# Documentation

Start with the [project README](../README.md) for an overview.

## Guides: how to use and build

| Doc | Read it when you want to... |
| --- | --- |
| [Consumer integration](guides/consumer-integration.md) | Add these components to your own ESP-IDF or CMake project. |
| [Arduino receive-only sketch](guides/arduino-sketches.md) | Compose the installed package with Runtime lifecycle, diagnostics and generic notifications. |
| [Arduino package](guides/arduino-package.md) | Generate/install the exact classic ESP32 package and compile its private probe. |
| [Host build](guides/host-build.md) | Build and test on your PC, including supported CMake versions and sanitizers. |
| [Bench firmware](guides/bench-firmware.md) | Build the isolated T-CAN485 bench firmware. Never connect it to a vehicle. |

## Design: how it works

| Doc | Covers |
| --- | --- |
| [Module boundaries](design/module-boundaries.md) | What each component owns and what it may depend on. |
| [Receive-only acquisition](design/receive-only-acquisition.md) | `can_bus` safety boundary and statistics semantics. |
| [Runtime work budgets](design/runtime-work-budgets.md) | `RuntimeConfig` limits, blocking behavior, and ESP32 results. |
| [Architecture contract checks](design/architecture-checks.md) | The `architecture_contracts` test that enforces these boundaries. |

## Performance: measurements

| Doc | Covers |
| --- | --- |
| [Synthetic runtime baseline](performance/runtime-synthetic-baseline.md) | Host and ESP32 runtime baseline using generated frames. |
| [Task residency and watchdog stress](performance/runtime-synthetic-bench.md) | ESP32 scheduler measurements under load. |

Raw data for these reports lives in [`evidence/`](evidence).

## Policies and releases

- [License and vehicle-data policy](policies/license-and-vehicle-data.md): what
  vehicle data may and may not be committed.
- [Release notes](releases): per-release change summaries.

# Module boundaries

The source repository is the reusable `esp32-vehicle-can-core`. It separates
portable value types from ESP-IDF transport and explicitly isolated bench
composition. A consuming controller repository owns all make/model and product
behavior.

## Ownership map

| Module | Owns | Boundary rule |
| --- | --- | --- |
| `vehicle_core` | Portable time, classic-CAN frame, signal, reading, notification, health, and decoder contracts | No make/model, CAN driver, board, ESP-IDF, or RTOS dependency |
| `can_bus` | ESP-IDF/TWAI receive-only lifecycle, bounded queue, and diagnostics | No runtime mode selector or business-level data-frame transmit operation |
| `bench_can_ack` | Explicit isolated-bench ACK binding | Normal mode is confined to the separately named T-CAN485 bench project |
| `tcan485-bench-ack-only` | Isolated T-CAN485 firmware composition | Must never be connected to a vehicle or treated as product firmware |
| downstream controller | Make/model decoder, telemetry service, lighting policy, board support, firmware, and protocol evidence | Consume this repository as a pinned Git submodule |

## Include and source compatibility

`vehicle_core` is the portable public umbrella. Its headers expose value types
and contracts only; they do not include transport, RTOS, ESP-IDF, board, or
controller paths. `can_bus` remains an ESP-IDF component and is consumed by
firmware compositions that explicitly opt into the receive-only transport.

Do not add a make/model header, DBC, board record, lighting implementation, or
product policy to this repository to satisfy a downstream application. Extend
the controller repository instead.

## Required boundary gate

The `architecture_contracts` CTest builds `vehicle_core` in an isolated
consumer and inspects its compile/dependency commands for ESP-IDF, RTOS,
transport, and component inputs. It also builds the real bench adapter tests.
Run it as part of the host CTest suite.

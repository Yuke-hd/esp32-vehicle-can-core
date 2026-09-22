# MCAN-17 module boundaries

The source repository keeps portable telemetry and decoder contracts separate
from transport and hardware composition. WeAct/accessory-controller ownership
was moved to the
[dedicated accessory repository](https://github.com/Yuke-hd/mazda-can-accessory-controller);
no source target here claims to build that product.

## Ownership map

| Module | Owns | Boundary rule |
| --- | --- | --- |
| vehicle_core | Portable time, frame, signal, reading, notification, and telemetry contracts | No Mazda model, decoder, CAN driver, board, ESP-IDF, or RTOS dependency |
| mazda | Mazda enums, freshness policy, state, candidate definitions, pure decoder APIs, and façade contracts | Public façade headers remain value-only and do not export internal decoder handoffs |
| vehicle_telemetry | Background acquisition façade, publication store, diagnostics, notifications, and generic private lighting sink hook | Hardware policy and concrete sinks remain outside the source repository |
| can_bus | Receive-only CAN lifecycle, bounded queue API, and diagnostics | No runtime mode selector or data-frame transmit operation |
| bench_can_ack | Explicit isolated-bench ACK binding | Normal mode is confined to the separately named T-CAN485 bench project |
| tcan485-bench-ack-only | Isolated T-CAN485 firmware composition | Must never be connected to a vehicle or treated as product firmware |

The generic private LightingSink and LightingUpdate handoff remain part of
vehicle_telemetry and mazda for downstream consumers. This source change
removes only the concrete accessory implementation and its policy/renderer
targets; it does not remove the generic contract.

## Include/source compatibility

vehicle_core remains the portable umbrella. mazda telemetry contracts continue
to expose only value-copy façade types, while raw decoder/service handoffs stay
under the explicit lib/mazda/internal_include boundary. Ordinary consumers
must not gain access to private state, frame internals, CAN drivers, board
records, lighting implementations, or RTOS headers transitively.

## Required boundary gate

The architecture_contracts CTest builds vehicle_core in an isolated consumer
project without Mazda or RTOS inputs, builds and runs the bench adapter tests,
and confirms that retired capture code has no active dependency. The separate
public_header_boundary and public_header_checker_regression tests preserve the
public-header closure and internal-access checks.

The full host CTest run is the required regression command. Passing it does
not claim ESP-IDF support, physical vehicle/bench acceptance, or hardware
lighting behavior.

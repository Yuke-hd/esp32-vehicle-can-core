# MCAN-13 isolated T-CAN485 BENCH_ACK_ONLY

## Purpose and hard boundary

`firmware/tcan485-bench-ack-only` is a separately named ESP-IDF project for an
LILYGO/TTGO T-CAN485 board on an isolated, protected classic-CAN bench. It is
not product or vehicle firmware and must never be connected to a vehicle. The
project name, component description, startup warning, and application-facing
`bench_can_ack` component all carry the `BENCH_ACK_ONLY` label.

The target uses TWAI normal mode so a compliant classic-CAN frame can be
acknowledged by the controller. It accepts frames through the existing
receive-only public API and keeps the hardware TX queue at zero. There is no
business-level frame transmit operation, TWAI handle, diagnostic operation, or
recovery API. The acceptance filter remains receive-all for bench protocol
observation; CAN FD is not enabled or supported.

After startup, the bench application waits on the public receive boundary with
a one-second timeout and drains at most 16 frames per batch. Timeouts are
silent so an idle bench does not spam the serial console. The first received
frame emits an immediate serial proof; subsequent output is aggregated to at
most one summary per second, containing only cumulative/interval counts and
the latest identifier format, identifier, DLC, and bus number. Payload bytes
are never logged. A 10 ms `vTaskDelay` follows every non-empty batch so a
permanently ready receive queue cannot starve the ESP-IDF idle task/watchdog;
`taskYIELD` alone is insufficient. Any non-timeout receive failure is reported
and the CAN component is stopped before the application exits.

The `bench_can_ack` component owns the bench's compile-time
`TWAI_MODE_NORMAL` and T-CAN485 CAN-pin binding. The shared `can_bus` component
contains only the receive engine and has no mode selector. The bench project
enumerates only `bench_can_ack`, and exposes no data-frame transmit operation.
Make/model controller applications are maintained separately in downstream
repositories that consume these generic components as pinned Git dependencies.

## Build checks

From an ESP-IDF v5.5.4 environment, build each project in its own directory:

```text
cd firmware/tcan485-bench-ack-only
idf.py set-target esp32
idf.py build
```

The host architecture gate checks the bench binding, zero TX queue, and
absence of transmit calls. Do not rename or package the bench output as
vehicle firmware.

## Physical isolation and test alternatives

Use only a current-limited, isolated bench supply with a correctly terminated
two-ended test bus. Keep the board disconnected from every vehicle harness and
from any unprotected automotive supply. Mark the board and its firmware
artifact `T-CAN485 BENCH_ACK_ONLY — ISOLATED BENCH ONLY`.

For final vehicle behavior, consult the strict listen-only target in the
downstream controller repository. This bench artifact provides no vehicle
behavior evidence.
No raw vehicle captures, VIN, credentials, precise location, or reconstructable
trip data belong in build evidence, Issues, PRs, or releases.

Integrated hardware ACK validation is not claimed by this software change; it
requires the physical bench procedure. It is not evidence for any
vehicle-side hardware or application.

# Arduino receive-only Runtime sketch

[`ReceiveOnlyRuntime`](../../examples/arduino/ReceiveOnlyRuntime/ReceiveOnlyRuntime.ino)
is a public consumer of the clean installed Arduino package. It injects the
actual `CanBusSource`, an application `FrameProcessor`, and an application
`Observer` into `Runtime`. It counts processed frames without decoding or
logging identifiers/payloads. Set its GPIO 27/26 binding and 500 kbit/s bitrate
for your isolated setup before uploading; these are example settings, not a
verified board pin recommendation. The application supplies exactly one driver
binding, with `TWAI_MODE_LISTEN_ONLY` and `tx_queue_len = 0`.

## Lifetime, lifecycle and diagnostics

Static dependencies precede `Runtime`, so they survive its worker and eventual
destruction. Construction is inert. `setup()` checks runtime configuration,
then starts the runtime.
Only the Arduino application task calls lifecycle methods. Do not independently
start/stop `can_bus` behind `CanBusSource`, add a second receive consumer, or call
runtime lifecycle operations from its worker observer. See
[acquisition ownership](../design/runtime-ownership.md).

Open a 115200-baud serial monitor. `loop()` processes at most one command per
iteration, prints an aggregate
diagnostic snapshot once per second, and requests a 10 ms application delay.
CAN acquisition/processing continues on the Runtime worker; `loop()` is not a
runtime pump. Serial output reports operation results plus lifecycle, transport
health, processed/dropped counts, fairness pauses and the observer frame count.
Enum values are defined by the installed
`vehicle_telemetry/lifecycle.hpp` and `vehicle_core/health.hpp` headers; lifecycle
values are Stopped=0, Running=1, Stopping=2, Faulted=3.

| Command | Behavior |
| --- | --- |
| `s` | Stop or retry incomplete cleanup. Report the returned result and lifecycle. |
| `r` | Stop, confirm successful cleanup and Stopped, then configure and restart. |
| `d` | Print an immediate diagnostic snapshot. |

A failed stop retains all dependency and observer contexts. The example reports
incomplete cleanup and accepts a later `s` retry. It never resets worker-owned
state or restarts until stop succeeds (or reports NotRunning) and the lifecycle
is Stopped; restart then resets the frame count.
A failed start invokes the same checked cleanup path. This normal sketch
reports real errors; it does not inject or prove a failed-stop scenario.

## Generic readings and synchronization

The observer counts processed frames in an atomic and exposes the count as a
generic `Reading<uint64_t>` copy for `loop()`. Counts use `FreshnessUnverified`
and `Observed`: receiving frames supplies no product signal validity or
freshness policy. No named reading facade or product availability evaluator is
supplied. Transport silence is independently visible in Runtime diagnostics.

This package no longer provides decoded-signal subscriptions or notification
channels; that ownership belongs to the application above CAN acquisition (see
the [0.3.0 release notes](../releases/notification-removal-0.3.0.md)). The
example's frame count is reset only after worker quiescence. These choices do
not promise arbitrary sketch thread safety.

Runtime calls observers synchronously on its worker. Keep observer and processor
work bounded: allocation, logging, waits, or a delayed observer can delay frame
processing. Cooperative budgets cannot preempt a call; a permanently blocked
observer prevents completion, and Runtime destruction waits for its worker.
The channel callback here also performs only a bounded copy, but a callback
count bound cannot limit the elapsed time of an arbitrary callback. Runtime
defaults remain 16 frames/2 ms batch, 512 receive returns/20 ms runnable work,
and a requested 1 ms pause rounded up to at least one tick. Actual delays depend
on the scheduler. See [work budgets](../design/runtime-work-budgets.md).

## Clean installed compilation and include order

Use the exact CLI 1.3.1/core 3.3.8/FQBN/toolchain configuration and clean staging
and installation commands in [the package guide](arduino-package.md), selecting
the current integrated source commit with `--revision <commit-sha>`. Then copy
the entire sketch folder outside the checkout and compile against that installed
package:

```sh
mkdir -p "$package_root/sketches/ReceiveOnlyRuntime"
cp examples/arduino/ReceiveOnlyRuntime/* "$package_root/sketches/ReceiveOnlyRuntime/"
python3 -c 'import json; print(json.load(open("tools/arduino/profile.json"))["language_option"])' \
  > "$package_root/sketches/ReceiveOnlyRuntime/build_opt.h"
fqbn=$(python3 -c 'import json; print(json.load(open("tools/arduino/profile.json"))["fqbn"])')
"$cli" --config-file "$package_root/config.yaml" compile --verbose \
  --fqbn "$fqbn" --build-path "$package_root/sketch-build" \
  "$package_root/sketches/ReceiveOnlyRuntime" > "$package_root/sketch-compile.log" 2>&1
```

The preparation command generates `build_opt.h` to select `-std=gnu++17` from
the agreed profile. The sketch's two `.cpp`
translation units include every canonical public header, plus the package
umbrella, with `Arduino.h` first and last. Separate `.cpp` files avoid Arduino's
automatic `.ino` include insertion masking the second order. Both assert C++17
and the ESP backend. Local macOS ARM64 builds require the documented native
ctags helper override from the package guide. These sketches are kept as public
repository consumers; the generated package source set is unchanged.

## Evidence boundary

The normal sketch and both include-order translation units compiled and linked
against a fresh installed package generated from integrated source revision
`96646daebdd4ec077e2e0a93f656e1a268c4dd37`, ZIP SHA-256
`6a4805267d2ec6923f4923b63b5e075bb2db13a353aa8764f8f25a5e7a8ec0a1`.
The selected macOS ARM64 profile (including the package guide's native ctags
helper) used 350728 bytes of flash and 41452 bytes of static RAM. The compile
database contained exactly the four installed project implementation files
and both header-order units; dependency files contained no repository source
path. Compilation asserted C++17/ESP_PLATFORM and emitted no warnings or errors.
These sizes describe this example and do not establish stack headroom.

Installed-package compilation/linking and include-order compatibility are build
evidence. No upload, CAN traffic, failure injection, delayed-loop/observer
timing, hardware quiescence, or physical acceptance is claimed here. Normal
serial commands provide an application exercise, not a bounded pass/fail
integration harness. The separate synthetic harness work under #95 must cover
silence, delayed work and injectable faults and report which checks actually
ran. Actual TWAI and isolated physical measurements remain separate evidence
under #97.

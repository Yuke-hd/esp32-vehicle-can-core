# Arduino acceptance evidence

This report tracks [#97](https://github.com/Yuke-hd/esp32-vehicle-can-core/issues/97)
and [#88](https://github.com/Yuke-hd/esp32-vehicle-can-core/issues/88). The
synthetic on-device run below passed; **physical CAN acceptance is outstanding**.
All P1–P8 measurements are required before either issue or the milestone closes.
Builds and synthetic checks cannot satisfy an unperformed physical row.

## Evidence boundary

| Evidence class | Recorded state | What it establishes |
| --- | --- | --- |
| Actual TWAI / physical isolated bench | NOT RUN, P1–P8 outstanding | No physical CAN acceptance claim. |
| Arduino synthetic on-device harness | PASS, 14/14 checks, one run | Real Runtime worker with software source/faults on the selected chip. |
| Final installed-package/sketch CI | PASS, three clean installed consumers | Compilation/linking and package isolation; no execution claim. |
| Host regression CI | PASS, host, both CMake versions and ASan/UBSan | Host execution and architecture checks; no ESP32 execution claim. |
| ESP-IDF v5.5.4 firmware CI | PASS, isolated bench and runtime-synthetic builds | Firmware compilation only; no execution claim. |

The board had no CAN connection. The synthetic harness initialized no CAN/TWAI
driver and exercised no transceiver, bitrate, interrupt buffering or bus fault.
It does not establish CAN load capacity, watchdog margin or resource coexistence.

[Final-D CI run](https://github.com/Yuke-hd/esp32-vehicle-can-core/actions/runs/38340608379)
passed all eight jobs for revision `0f300bc00a13c77d1fa767358c7df150e602aafa`:
the installed private probe, ReceiveOnlyRuntime and RuntimeSyntheticHarness;
host, CMake 3.20.5/4.4.3, ASan/UBSan; and the two IDF firmware builds.
This is the final software CI input. The separately measured synthetic run
below retains its earlier immutable source/package/image identity.

## Synthetic run provenance

The run was collected on 2026-10-13 at approximately 03:06 UTC from a clean
installed package. The chip was ESP32-D0WDQ6-V3, revision 3.1, with a 40 MHz
crystal. No unique device identifier is retained.

| Input / artifact | Immutable value |
| --- | --- |
| Source revision | `72a2a044fe43b466c09361fd788168e57dcbfaee` |
| Staged package ZIP SHA-256 | `f00b746704a6ce20fbc766deb0d85651d5c2a7a9e4a88e6f053307f599b17561` |
| RuntimeSyntheticHarness sketch SHA-256 | `2fea36da2247da9aabf88b2d85622b0b17ec114cee7a01c5bb1da7a1cb3c1f13` |
| Application `.bin` SHA-256 | `f7e27e8cd3a029c89f6c9542ebf2e66866c7f2451ed2207e72907d538725b5fa` |
| Profile content SHA-256 | `805ccf13626fbb46c6024f749a6b3a6c9a0563d72d24e88218783e3da7267a1a` |
| Staging generator content SHA-256 | `b8e412c7a172011bd37a601711a677a18fe84d666cdd810ff761657a70d62756` |

The installed `PROVENANCE.json` ties source-file hashes and packaging inputs to
that revision. Its `ARDUINO_PROFILE.json` records CLI 1.3.1 (`08ff7e2b`),
`esp32:esp32@3.3.8`, bundled IDF v5.5.4, `esp-x32@2601`, GCC 14.2.0
(`crosstool-NG esp-14.2.0_20260121`) and `-std=gnu++17`.
Use the [immutable profile](https://github.com/Yuke-hd/esp32-vehicle-can-core/blob/72a2a044fe43b466c09361fd788168e57dcbfaee/tools/arduino/profile.json)
for the exact FQBN, core archive hash and resolved SDK settings.

Board options select ESP32 Dev Module, 240 MHz, QIO/80 MHz/4 MB flash, default
partitions, no PSRAM/debug, loop/event cores 1 and upload 921600. SDK `qio_qspi`
has 1000 Hz ticks, dual core, C++ exceptions enabled/RTTI disabled, interrupt
watchdog 300 ms including CPU1, task watchdog enabled and listen-only erratum
fix enabled. The SDK default CPU setting is 160 MHz; board compilation selects
240 MHz. The application image used 340,904 bytes flash and 38,908 bytes static
RAM; those sizes are specific to this harness.

## Sanitized synthetic results

The [immutable harness](https://github.com/Yuke-hd/esp32-vehicle-can-core/blob/72a2a044fe43b466c09361fd788168e57dcbfaee/examples/arduino/RuntimeSyntheticHarness/RuntimeSyntheticHarness.ino)
runs bounded named checks once from `loop()`. Receive calls, callbacks, maximum
callback wall time and budget pause attempts below are diagnostic snapshots for each
check; they are not additive independent trials. Zero timing means no observed
callback in that snapshot. Pause attempts do not prove a kernel context switch.
Summary: `checks=14 expected=14 failures=0 result=PASS`.

| Check | Result | Receive calls | Callbacks | Max callback µs | Pause attempts |
| --- | --- | ---: | ---: | ---: | ---: |
| setup_configure | PASS | 0 | 0 | 0 | 0 |
| invalid_configuration | PASS | 0 | 0 | 0 | 0 |
| start_failure | PASS | 0 | 0 | 0 | 0 |
| initial_silence | PASS | 9 | 0 | 0 | 2 |
| frame_then_silence | PASS | 6 | 1 | 1 | 0 |
| delayed_loop_bounded_observer | PASS | 2566 | 2565 | 6 | 5 |
| configure_running_rejected | PASS | 2933 | 2932 | 6 | 5 |
| observer_cooperative_nonpreemption | PASS | 1 | 1 | 34677 | 1 |
| receive_fault | PASS | 1 | 0 | 0 | 0 |
| processor_fault | PASS | 1 | 1 | 1 | 0 |
| failed_stop_retains_context | PASS | 54 | 54 | 1 | 0 |
| stop_retry_quiescence | PASS | 55 | 54 | 1 | 0 |
| restart_after_quiescence | PASS | 59 | 58 | 1 | 0 |
| final_cleanup | PASS | 59 | 58 | 1 | 0 |

Initial silence means `AwaitingTraffic` before a first frame; silence after a
frame is a separate check. Observers run synchronously on the worker. The slow
callback measurement demonstrates that cooperative budgets do not preempt a
callback; it does not guarantee timing or delivery for a permanently blocked
observer. See [harness usage](arduino-synthetic-harness.md) for reproduction and
[sketch usage](arduino-sketches.md) for lifecycle and synchronization.

## Required physical measurements

All rows are **NOT RUN / outstanding**. No transceiver, CAN peer, pin mapping,
bitrate, terminated wiring topology or safe fault-injection path was confirmed
for this run. Prepare an isolated, protected, correctly terminated bench with
a compatible transceiver and controlled classic-CAN source. The DUT must use
exactly one application-owned listen-only binding with `tx_queue_len=0`.
Provide a separate ACK-capable bench peer if the source requires ACKs; the DUT
must remain listen-only. Never use vehicle wiring or live-vehicle fault injection.

| ID | Required case / result | Method and pass evidence to record |
| --- | --- | --- |
| P1 | Actual CAN receive — NOT RUN | Confirm pins/bitrate/mode/TX queue and single binding; send a controlled workload and reconcile received aggregate counts with the peer. |
| P2 | Idle/watchdog — NOT RUN | Run a declared idle duration; record worker/idle progress, maximum gaps, reset/watchdog outcomes and measured margin against the configured watchdog. |
| P3 | Tick/scheduling — NOT RUN | Record resolved tick conversion and worker/observer timing; compare declared delayed-loop cases and record scheduler-dependent delays against predeclared bounds. |
| P4 | Budgeted load — NOT RUN | Use bounded peer rates/durations; measure frame batches and receive-call limits against 16 frames/2 ms and 512 calls/20 ms defaults; measure 1 ms requested pause rounding to at least one tick. |
| P5 | Loss/statistics — NOT RUN | Declare input counts, rates and queue capacities; reconcile received/processed/dropped counts, overflow visibility and diagnostic snapshots for stable and overloaded cases. |
| P6 | Lifecycle/faults — NOT RUN | Exercise real start/stop/retry/restart and quiescence; use safe external-owner conflict/terminal-fault injection paths and verify only acquired resources are released. |
| P7 | Silence — NOT RUN | Measure initial no-frame and post-frame silence separately, with declared durations and the generic transport diagnostics; verify recovery when controlled traffic resumes. |
| P8 | Resource coexistence — NOT RUN | Name an unrelated application resource and safe injection method; verify its behavior across start, stop, failures and retries against declared baseline bounds. |

For each row, record immutable source/package/image/profile hashes, compiler and
SDK settings, topology/pins/bitrate/termination, workload and observer costs,
duration/repetitions, predeclared bounds, sanitized measured aggregates and an
explicit PASS or FAIL. An unavailable injection or measurement remains NOT RUN.
Choose bench-specific timing/load bounds before measuring; do not infer them
from this synthetic sample. Retain all repetitions rather than the best run.

## Remaining work and failure routing

Confirm the missing bench configuration and injection capabilities, then measure
every physical row on the agreed package/configuration. Return runtime failures
to #94, package failures to #93, sketch failures to #95 and CI failures to #96;
rerun affected rows after a fix. Keep #97 and #88 open until all required rows pass.
Generic `Reading`/`Notification` and header-only `NotificationChannel` composition
do not imply a product reading/evaluator or an independent Runtime callback task.
Retain only sanitized aggregates and provenance: no payloads/raw captures,
credentials, device MAC, precise trip/location data or vehicle fault injection.

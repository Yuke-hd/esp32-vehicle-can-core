# Runtime acquisition ownership

`Runtime` borrows its `AcquisitionSource`, `FrameProcessor`, `Observer`, and
optional monotonic clock. Keep these objects alive until the runtime is
stopped and destroyed. Construction, configuration, and stopped diagnostics
do not start acquisition or invoke processing/observers. Hardware setup and
worker creation occur only in `start()`.

## CAN source cleanup

`CanBusSource` wraps the process-wide `can_bus` acquisition. Serialize all
application CAN lifecycle calls, including calls through different source
instances. Once a source acquires the bus, stop it through that same source;
do not independently stop/restart the singleton behind its ownership claim.
One task consumes frames. Stop that consumer before restarting acquisition.

A source acquires cleanup responsibility after a successful start, or after
its own driver/task startup failure retains resources. An internal startup
outcome records that responsibility within the operation, before a later
caller can change the singleton lifecycle; cleanup never infers ownership
from a later global lifecycle sample.
Rejected starts (`AlreadyStarted`, `Stopping`, or `Faulted`) do not give a new
source cleanup responsibility for another acquisition. An installation
failure leaving `can_bus` stopped, including an external TWAI installation
conflict, also acquires no responsibility. Runtime's defensive partial-start
cleanup therefore leaves these owners untouched.

`stop()` and `retry_stop()` return `NotRunning` without calling the singleton
when this source has no cleanup responsibility. A failed owned stop retains
that responsibility so a later retry can release the remaining task/driver.
Only successful cleanup or a `NotStarted` reconciliation releases it. A
repeated rejected `start()` on the owning source preserves its existing claim.

Copy construction copies configuration only; it does not clone cleanup
responsibility. Rvalue construction uses this same copy contract. Assignment
copies configuration while retaining the destination's cleanup responsibility.
The assigned configuration applies on its next start. Source copies do not
provide an additional CAN consumer or driver.

The application still supplies exactly one
`can_bus::internal::configure_driver` binding. Arduino receive-only bindings
must select `TWAI_MODE_LISTEN_ONLY` and `tx_queue_len = 0`. The generic runtime
owns neither board/bootstrap initialization nor application peripherals, LEDs,
or transceiver power. Applications retain those resources across startup,
cleanup failures, retries, and shutdown.

## Arduino task and callback assumptions

The selected milestone configuration is Arduino CLI 1.3.1,
`esp32:esp32@3.3.8` (bundled ESP-IDF v5.5.4), classic ESP32 Dev Module, QIO
flash, no PSRAM, 240 MHz, and the Xtensa GCC 14.2.0 toolchain
`esp-x32@2601`. Package/probe validation selects C++17 explicitly. The actual
`qio_qspi/include/sdkconfig.h` sets two FreeRTOS cores and
`CONFIG_FREERTOS_HZ=1000`; `FreeRTOSConfig.h` sets `configMAX_PRIORITIES=25`.
These are configuration/header audit facts, not physical timing evidence.

| Worker | Stack | Priority | Creation affinity |
| --- | --- | --- | --- |
| Runtime processor/observer | 4,096 bytes | `tskIDLE_PRIORITY + 1` (1) | Unpinned `xTaskCreate` |
| CAN receiver | 4,096 bytes | `configMAX_PRIORITIES - 2` (23) | Unpinned `xTaskCreate` |

The bundled `freertos/task.h` defines stack depths in bytes and implements
`xTaskCreate` with `tskNO_AFFINITY` on the dual-core configuration. Arduino
`LoopCore=1` and `EventsCore=1` do not pin these tasks. Dependency stack use,
including the observer, must fit the worker stack; actual high-water marks and
scheduler behavior require isolated hardware measurements. The bundled
interrupt watchdog is 300 ms, and the task watchdog is enabled. The runtime
does not feed or relax either watchdog.

The unchanged [work-budget contract](runtime-work-budgets.md) defaults to
16 frames/2 ms per batch, 512 receive returns/20 ms runnable work, and a 1 ms
requested pause rounded up to at least one tick. At the selected 1,000 Hz the
requested default is one tick; actual duration depends on tick phase and the
scheduler. CAN driver receive polling requests 10 ticks, and task-stop
acknowledgement requests up to 100 ticks on this configuration.

Observers run synchronously on the Runtime worker: the frame observer is
followed by the diagnostic observer for a completed frame. Stop finishes the
current transaction before the next frame. Cooperative budgets cannot
preempt acquisition, statistics, processing, or callbacks. A permanently
blocked observer prevents worker completion and makes delivery/timing
promises impossible; destruction waits for that worker to finish. Keep all
borrowed dependencies alive during incomplete cleanup and retry.

## Verification boundary

`components/vehicle_telemetry/tests/can_bus_source_tests.cpp` runs production
`CanBusSource` and host `Runtime` against synthetic `can_bus` operation
outcomes. It proves inert borrowing, conflict isolation, partial-start and
failed-stop ownership, retry/restart, configuration-copy behavior, and host
worker quiescence. The existing portable CAN lifecycle tests cover retained
driver/task ownership; runtime/baseline tests cover cooperative work budgets
and completion of a slow synchronous observer. Architecture checks enforce
the generic module and application binding boundary.

These tests do not execute the TWAI implementation or the ESP scheduler.
Installed-package compilation and native IDF builds prove compile/link
compatibility only. Public installed sketches must exercise the actual
injected dependencies and single application binding. Isolated hardware
acceptance remains responsible for real TWAI conflicts, unrelated-peripheral
preservation, cleanup/retry timing, stack headroom, scheduler progress, and
quiescence on the selected configuration. No physical acceptance is claimed
by this ownership fix.

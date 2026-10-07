# Runtime work budgets (#133)

The worker owns processing and delivers each completed processor result to the
frame observer, then the diagnostic observer. `RuntimeConfig` now bounds work
between checkpoints and between positive blocking opportunities. These controls
are generic; no decoder, application policy, new task, priority change, fixed
core affinity or watchdog configuration is introduced.

## Configuration contract

New fields are appended after receive/silence controls, preserving existing
aggregate initialization such as `{10, 100'000}`. All bounds are inclusive;
`configure()` returns `InvalidConfiguration` for zero or excessive values, or
when a batch limit exceeds its corresponding runnable limit.

| Field | Default | Valid range | Meaning |
| --- | --- | --- | --- |
| `max_frames_per_batch` | 16 | 1–65,535, no greater than runnable calls | Complete frame transactions before a batch checkpoint. |
| `max_batch_time_us` | 2,000 | 1–1,000,000, no greater than runnable time | Platform wall time before a batch checkpoint. |
| `max_runnable_receive_calls` | 512 | 1–65,535 | Total receive returns before a blocking opportunity, including immediate timeouts. |
| `max_runnable_time_us` | 20,000 | 1–1,000,000 | Accumulated platform wall time before a blocking opportunity. |
| `budget_pause_ms` | 1 | 1–1,000 | Requested positive backend pause; ESP rounds up to ticks. |

The defaults are finite and cannot be disabled with zero. Existing receive
poll/silence validation remains unchanged. Configuration remains immutable
while running. Fairness uses the real platform monotonic timer, independently
of the borrowed liveness clock; a frozen/backwards liveness clock cannot disable
the finite receive-call fallback.

A processing batch is a finite inner loop. A non-frame receive ends that batch;
frame-count or inclusive elapsed-time exhaustion also ends it. The checkpoint
refreshes silence diagnostics under the existing diagnostic lock without
emitting an additional observer callback. Stop is checked after acquisition
and after each complete transaction. Terminal source/processor faults update
and publish diagnostics in their existing order and exit before any pause.

Batch checkpoints **never** clear the total runnable call count or time origin.
The total budget accumulates across batches, frames and timeouts, and takes
precedence when both boundaries coincide. It resets only after the selected
positive backend pause completes. Actual blocking inside an arbitrary source
cannot be inferred, so even sparse sources can receive a conservative extra
pause after total elapsed time is exhausted.

## Blocking and cancellation

ESP uses positive `vTaskDelay(1)` chunks. The requested tick count is
`ceil(budget_pause_ms * configTICK_RATE_HZ / 1000)`, always at least one tick.
Each chunk removes the worker from the ready set and checks cancellation before
the next chunk. No task notification slot is consumed: arbitrary injected
sources/processors may use notifications themselves. `taskYIELD()` is
insufficient because it does not guarantee lower-priority idle execution.
The implementation follows the official [ESP-IDF 5.5.4 FreeRTOS delay and
scheduler documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/api-reference/system/freertos_idf.html)
and [`tasks.c` delayed-list implementation](https://github.com/espressif/esp-idf/blob/v5.5.4/components/freertos/FreeRTOS-Kernel/tasks.c),
with no watchdog feeding or relaxation.

The default requests one tick at both 1,000 Hz and 100 Hz. Tick phase and
scheduler activity determine actual wall duration; this is not a promise of
exactly 1 ms. At 100 Hz a tick is 10 ms. Pausing after every 16-frame batch would
limit sustained capacity severely; the default instead permits up to 512
receive calls or 20 ms of accumulated work before requesting a pause. Hardware
measurements determine which synthetic arrival points remain supported.

Host uses a condition-variable timed wait. Stop updates the predicate and
notifies under the same mutex as wait entry, preventing a lost wakeup. This
also applies when source cleanup reports a timeout and during destruction.
ESP stop during a budget pause waits at most the current tick before the worker
observes cancellation. Total `Runtime::stop()` time also includes source
cleanup, scheduling delay and the public completion-poll loop; it can therefore
span more than one tick.

These are cooperative budgets. They cannot interrupt `receive()`, `statistics()`,
`process()` or either observer. Each dependency must honor its own bounded
contract. A time threshold can be exceeded by one complete receive/processor/
statistics/observer transaction plus checkpoint overhead. The processor and
both callbacks still finish that transaction when stop arrives during an
observer; no subsequent frame begins. A callback that never returns prevents
worker completion regardless of budget configuration.

`TransportDiagnostics::work_budget_pauses` counts selected pause attempts and
resets on start. Cancellation can interrupt an attempt. This bounded counter
is useful for work/lifecycle tests but does not prove a kernel block, idle
execution or maximum CPU residency; those require scheduler evidence.

## Verification and consumer rollout

Deterministic policy tests cover infinite ready work across multiple batches,
immediate non-frame returns, coincident boundaries, inclusive deadlines and
frozen/backwards samples. Runtime integration tests exercise ready frames and
immediate timeouts with a frozen liveness clock, cancel a long host pause,
prioritize source/processor faults over blocking, and finish a current slow
observer transaction without processing another frame. Negative mutations of
the exact call boundary, total-budget predicate and host wakeup are detected.
Host fairness is not used as evidence of ESP idle progress.

The [#132 baseline](runtime-synthetic-bench.md) remains an unchanged-runtime
reference. Its `max_checkpoint_wall_span_us` observes **source wait requests**,
not Runtime budget pauses. A ready source can therefore still report a whole-run
wall span after this change while dispatcher-measured worker residency is
bounded. Compare actual trace/idle/watchdog fields with identical workload,
diagnostics, optimization, tick rate and optional timing mode.

## Authorized ESP32 results (2026-10-07 UTC)

The public evidence contains [108 candidate rows](../evidence/runtime-budgets-esp32/aggregates.csv),
[36 variance groups](../evidence/runtime-budgets-esp32/statistics.csv),
[18 manifests](../evidence/runtime-budgets-esp32/provenance.jsonl),
[resolved common settings](../evidence/runtime-budgets-esp32/settings.json), and
[a fresh six-row paired baseline](../evidence/runtime-budgets-esp32/paired-baseline.csv)
with its [manifest](../evidence/runtime-budgets-esp32/paired-baseline-provenance.json).
The original #132 corpus remains a separate comparison; none is overwritten.
Raw serial/flash logs, full sdkconfigs and device identifiers are excluded.

Candidate images used committed executable source `d7b2cce`; subsequent docs
and the telemetry 0.2.0 manifest do not change executable sources. The paired
baseline used `68778f1`, with unchanged #132 firmware behavior. Each image has
its app/config SHA-256 and exact source revision. Common settings are clean
ESP-IDF 5.5.4, Xtensa GCC 14.2.0, ESP32-D0WDQ6-V3 rev3.1, 240 MHz, performance
optimization, both cores, no power management, a 5-second task watchdog checking
both idle tasks with panic off, and a 300 ms interrupt watchdog. The isolated
board was USB powered with CAN-H/CAN-L disconnected. No CAN driver was used.

Each image ran three nominal 7-second repetitions in each timing mode, with
100 ms recovery. `run_us` includes start/stop; tick phase can shorten nominal
duration by one tick. Candidate durations were 6.995578–7.009929 seconds, all
longer than the unchanged watchdog period. Processor busy work was 10 µs;
slow-observer work was 200 µs in each callback. Both diagnostic callbacks and
all five default Runtime controls were identical throughout. Four extra images
changed only tick rate to 100 Hz and selected workload.

Every candidate row conserved queue counts, remained within capacity 128,
matched diagnostic result counts and avoided the frame cap. Every capture had
one expected initial boot and no extra boots, Guru Meditation, abort or fixture
errors. **All 108 candidate rows had zero watchdog events/warnings and idle
progress on both cores.** Serial warnings independently matched ISR counters.
Nonempty runs sampled executing/affinity core 1 after float work; empty
cancellation runs have no FPU samples and can trace the unpinned worker on either
core. Residency maxima below include both cores and ISR/scheduler overhead.

### Paired continuously ready comparison at 1,000 Hz

| Timing | Before mean frames/s (sample SD) | After mean frames/s (sample SD) | Before/after max CPU1 idle gap ms | Before/after max worker residency ms | Before/after watchdog events |
| --- | --- | --- | --- | --- | --- |
| 0 | 34,210.7 (11.0) | 30,118.7 (2.3) | 7,000.240 / 16.630 | 6,999.907 / 16.120 | 3 / 0 |
| 1 | 29,221 (0) | 26,942 (0) | 7,000.005 / 19.235 | 6,999.915 / 18.287 | 3 / 0 |

The worker now gives idle tasks repeated opportunities during saturated load,
with roughly 12.0%/7.8% lower ready throughput. This tradeoff includes budget
checks and blocking; it is not presented as a decode optimization. Integer
frames/s reporting can produce zero sample SD. Ready/mixed still show roughly
7-second source-checkpoint wall spans, because those fields do not observe
Runtime pauses. Actual dispatcher residency, idle gaps and watchdog outcomes
supply the scheduling evidence.

### Sampled stable arrival points at 1,000 Hz

All rows below had no drops/watchdog incidents in both timing modes. Batch means
modeled arrivals per 1 ms. Means are mode 0 / mode 1; maxima cover all six runs.
Exact ranges and sample SD for every group are retained in the statistics CSV.

| Workload | Mean frames/s (0 / 1) | Max CPU1 idle gap ms | Max worker residency ms | Max backlog | Max stop ms |
| --- | --- | --- | --- | --- | --- |
| burst 1 | 999 / 999 | 1.145 | 0.541 | 2 | 1.030 |
| burst 4 | 3,999 / 3,999 | 1.361 | 0.633 | 8 | 1.030 |
| burst 8 | 7,998 / 7,997 | 2.099 | 1.137 | 24 | 2.013 |
| burst 16 | 15,997.3 / 15,997 | 3.363 | 2.363 | 43 | 1.014 |
| slow observer 1 | 999 / 999 | 2.984 | 1.984 | 2 | 1.026 |
| slow observer 2 | 1,999 / 1,999 | 19.388 | 18.388 | 7 | 1.026 |
| cancel while waiting | 0 / 0 | 1.000 | 0.268 | 0 | 1.030 |

The 16,000 modeled arrivals/s point is retained: mode 0 ranged 15,997–15,998
frames/s (SD 0.6), and mode 1 reported 15,997 each time. This supports only the
sampled 1/4/8/16 burst and 1/2 slow-observer points, not every intermediate load
or a response-time SLA. Slow observer 2's maximum idle gap/residency increased
from #132's 15.402/14.401 ms to 19.388/18.388 ms despite unchanged 1,999 frames/s
and no drops/watchdog events. Fairness metrics did not improve at every load.

The ready/mixed and burst/slow overloads all avoided watchdog incidents. Queue
capacity remains independent: burst 32/64 reached 128 with maximum per-run drops
35,273/259,272; slow observer 64 dropped up to 432,918. They are excluded from the
no-drop arrival envelope. Across all 84 candidate rows at 1,000 Hz, maximum
worker residency was 20.415 ms, CPU1 idle gap 21.345 ms and total stop 2.013 ms.
The residency threshold can overshoot by a current full transaction; an idle
gap is not the same metric as the 20 ms runnable budget.

Trace-on/off ready throughput means were 30,118.7/30,119 in mode 0 and identical
26,942 in mode 1; burst 16 means were identical at reported precision. These
limited pairs do not establish a universal instrumentation overhead bound.
Optional timing mode still changes saturation capacity; compare identical modes.
Receive/processor/observer cost fields retain wall-time/ISR semantics.

### Tick-rate tradeoff at 100 Hz

| Workload | Mean frames/s (0 / 1) | Max drops per run | Max backlog | Max CPU1 idle gap ms | Max worker residency ms | Max stop ms |
| --- | --- | --- | --- | --- | --- | --- |
| ready | 25,571 / 25,563 | 0 | 1 | 25.362 | 18.191 | 10.010 |
| burst 4 | 3,987.7 / 3,990 | 0 | 78 | 13.047 | 3.087 | 10.033 |
| burst 16 | 10,642 / 8,910 | 49,221 | 128 | 19.318 | 9.364 | 10.034 |
| cancel while waiting | 0 / 0 | 0 | 0 | 10.000 | 0.274 | 10.034 |

All 24 lower-tick rows had both-core idle progress and no watchdog incidents.
The 4,000 arrivals/s point had no drops (mode 0 range 3,983–3,990, SD 4.0).
The 16,000 point overflowed and is not supported at these 100 Hz settings.
Coarse source waits and Runtime pauses accumulate modeled arrivals into the
fixed queue; the table does not isolate a single cause for each drop. Idle-hook
gaps can exceed the runnable time allowance because they also reflect blocked
ticks and hook sampling phase. Total stop includes completion polling, so its
observed 10.034 ms maximum is not a one-tick scheduling guarantee.

These observations characterize the generic synthetic workload and current
scheduler configuration. They establish neither an actual CAN driver envelope
nor controller decoder/vehicle readiness. Reproduce with the exact manifest
settings; downstream consumers must measure their own callback and acquisition
costs before selecting a supported production load.

Consumers must rebuild all dependent components for the appended config and
diagnostic fields, then pin the reviewed complete core commit. See the prepared
[0.2.0 release notes](../releases/runtime-budgets-0.2.0.md). Release/tag publication
is pending the reviewed merge; no published release is implied here.

# Synthetic task residency and watchdog stress (#132)

This extends the [runtime baseline](runtime-synthetic-baseline.md) with an
optional ESP32 scheduler measurement and configurable stress duration/load.
Production Runtime scheduling and watchdog configuration are unchanged. The
software target has no CAN component or pins. Physical execution requires an
explicitly authorized isolated USB-powered board with CAN-H/CAN-L disconnected.
The measurements below were collected on that authorized bench.

## Measurement definition

`CONFIG_BASELINE_SCHEDULER_TRACE=y` links only this target with
`--wrap=vTaskSwitchContext`. ESP-IDF 5.5.4's Xtensa dispatcher calls that
function before restoring the selected task; the wrapper calls the real kernel
function once. It reads the old/new handles and samples a common timestamp after
selection. Only a real change of selected task closes the old interval and
starts the new interval. Tick reselection of the same task keeps the interval
continuous. A final run-boundary sample closes any still-active interval once.

The maximum is **continuous worker task residency between selected-task
changes**. It includes intervening ISR work, same-task scheduler reselections,
context restore, wrapper overhead and the outgoing selection's kernel work.
It is an upper bound on worker CPU execution, not interrupt-free CPU time.
The existing source-wait checkpoint wall span remains a separate metric.

The worker is recognized on its first selection using the configured truncated
`vehicle_telemetry` task name; this bench has exactly one such task. That captures
the initial pre-FPU prefix without a late callback registration. The trace
requires ESP32 IDF FreeRTOS, at least 16 task-name bytes and getters in IRAM.
Before accepting a trace image, inspect its ELF: `_frxt_dispatch` must call
`__wrap_vTaskSwitchContext`, which must call `vTaskSwitchContext` exactly once;
its complete called helper chain must be IRAM-safe. No lock is held across the
kernel call. Timestamp/counter updates take a short critical-safe lock and
allocate/print nothing.

Appended CSV fields report trace enabled, each core's maximum residency and
number of closed worker intervals, same-task reselections, and watchdog events.
Trace-disabled/host residency fields are unavailable zeros. A traced run that
processes frames but has no closed worker interval is invalid measurement.
The trace wrapper has overhead; compare trace-on/off images with identical
sdkconfig, workload and optional timing mode rather than treating it as free.

The documented `esp_task_wdt_isr_user_handler` callback increments a bounded
lock-free counter during each run. It does not feed, suppress or reconfigure the
watchdog. Keep serial watchdog warnings as an independent cross-check. Default
watchdog behavior warns without panic/reset, so lack of reset does not imply a
pass. A watchdog event or missing idle progress fails the tested fairness/load
case even when a later aggregate row is produced.

## Stress configuration

The target's Kconfig menu selects duration, repetitions, scenario, burst batch
and interval, synthetic processor/slow-observer work, and scheduler tracing.
Defaults retain the baseline's 250 ms runs. Scenario numbers are 0 for all,
1 ready, 2 mixed, 3 burst, 4 slow observer and 5 cancellation while waiting.
Batch/interval settings affect burst and slow-observer cases; ready/mixed
remain continuously replenished. Both optional timing modes always run.

For a reproducible compile-only stress image, create a separate defaults file:

```text
CONFIG_BASELINE_DURATION_MS=7000
CONFIG_BASELINE_REPETITIONS=3
CONFIG_BASELINE_SCENARIO=3
CONFIG_BASELINE_ARRIVAL_BATCH=1
CONFIG_BASELINE_ARRIVAL_INTERVAL_US=1000
CONFIG_BASELINE_PROCESSOR_WORK_US=10
CONFIG_BASELINE_OBSERVER_WORK_US=200
CONFIG_BASELINE_SCHEDULER_TRACE=y
```

Pass the repository defaults followed by this file through
`SDKCONFIG_DEFAULTS` to a fresh ESP-IDF 5.5.4 build and record resolved sdkconfig
and binary hash. Repeat with batches 1, 4, 8 and 16 per 1 ms (1,000/4,000/8,000/
16,000 modeled arrivals per second), then ready, mixed and slow-observer cases.
Run matching trace-disabled images to characterize instrumentation impact.
Seven seconds exceeds the default 5-second watchdog period; three repeats in
both timing modes exercise each selected load for about 42 seconds plus
recovery gaps. The controller remains at idle+3 and the worker at idle+1.
Do not relax watchdogs or change Runtime priorities to make a load pass.

A supported synthetic envelope must name the exact tested loads/settings and
require no watchdog events or drops and progress on both idle cores. Report
idle gaps, backlog, stop latency and trace residency without inventing an SLA.
Report mean/range/sample
standard deviation for repeated aggregates. Source arrivals are modeled at
receive checkpoints; this does not establish an actual CAN driver load envelope
or vehicle readiness. Keep observed overload failures in the report rather
than dropping them from the supported-range decision.

## Authorized ESP32 evidence (2026-10-07 UTC)

The public corpus contains [84 aggregate rows](../evidence/runtime-synthetic-esp32/aggregates.csv),
[28 variance groups](../evidence/runtime-synthetic-esp32/statistics.csv),
[14 per-image manifests](../evidence/runtime-synthetic-esp32/provenance.jsonl)
and [common resolved settings](../evidence/runtime-synthetic-esp32/settings.json).
It contains synthetic counts/durations, source revisions and binary/config hashes;
raw UART/flash logs, full sdkconfigs and device identifiers are excluded.
The earlier 250 ms smoke run is excluded from this corpus.

Each image ran three 7-second repetitions in each optional timing mode, with
100 ms recovery between runs. Total measured time was 588 seconds. Hardware
was ESP32-D0WDQ6-V3 rev3.1, USB powered with CAN-H/CAN-L disconnected. The clean
ESP-IDF v5.5.4 build used Xtensa GCC 14.2.0, performance optimization, 240 MHz,
1 kHz ticks, both cores, power management off, and FreeRTOS functions in IRAM.
Task watchdog timeout was 5 seconds with both idle tasks checked and panic off;
interrupt watchdog timeout was 300 ms. Neither watchdog was relaxed or fed by
the fixture. Diagnostics invoked both callbacks. Processor work was 10 µs;
slow-observer work was 200 µs in **each** callback.

The first ready-on/off images used source `2827d72`; all other images used
`5e9a4df`, whose only changes were CI coverage and a trace defaults preset.
Firmware implementation was identical. The ELF was checked for the actual
Xtensa dispatcher-to-wrapper call and IRAM-safe helpers. Each manifest records
its exact source revision, resolved sdkconfig hash and app hash. Reproduce a
point using its `settings` through the defaults-file procedure above.

All 84 rows conserved generated = consumed + dropped + final backlog, stayed
within the 128-frame queue, matched diagnostic result counts, and avoided the
frame cap. All 14 captures contained exactly one expected initial boot and no
extra boots, Guru Meditation, abort or fixture errors. Serial task-watchdog
warnings matched the per-run ISR counters exactly. Every nonempty run sampled
both actual executing core and post-FPU affinity on **core 1**; the empty
cancellation case has no FPU samples and its unpinned worker was traced on both
cores (maximum residency 0.207 ms on CPU0). This confirms float-induced pinning for
this workload and configuration.

### Observed stable synthetic points

These traced points had no watchdog events or drops in all six repetitions,
with idle progress on both cores. Batch means arrivals per 1 ms. Maxima below
cover both timing modes and all three repetitions; throughput entries give
mode 0 / mode 1 means. Exact ranges and sample standard deviations are in the
statistics CSV; integer frames/s reporting can produce zero variance.

| Workload | Mean frames/s (0 / 1) | Max CPU1 idle gap ms | Max worker residency ms | Max backlog | Max stop ms |
| --- | --- | --- | --- | --- | --- |
| burst 1 | 999 / 999 | 1.109 | 0.470 | 1 | 1.034 |
| burst 4 | 3,999 / 3,999 | 1.170 | 0.558 | 4 | 1.034 |
| burst 8 | 7,998 / 7,997 | 1.837 | 0.836 | 16 | 1.034 |
| burst 16 | 15,997.3 / 15,996 | 2.899 | 1.899 | 36 | 1.013 |
| slow observer 1 | 999 / 999 | 3.006 | 2.006 | 2 | 1.031 |
| slow observer 2 | 1,999 / 1,999 | 15.402 | 14.401 | 5 | 1.034 |
| cancel while waiting | 0 / 0 | 1.000 | 0.207 | 0 | 1.030 |

CPU0's maximum idle gap across these points was 1.180 ms. At burst 16, mode 0
throughput ranged 15,997–15,998 frames/s (sample SD 0.6); mode 1 reported
15,996 in each repetition. The slow-observer 2 point is watchdog/backlog stable
under the stated criteria but has 15.402 ms idle gaps. None of these results
establish a response-time or fairness SLA. They support only these sampled
synthetic settings, not every intermediate load, a CAN driver envelope, or
vehicle readiness.

### Observed overload and instrumentation effects

| Workload | Mean frames/s (0 / 1) | Watchdog events (0 / 1 totals) | Max dropped frames per run | Max CPU1 idle gap ms | Max worker residency ms |
| --- | --- | --- | --- | --- | --- |
| continuously ready, trace on | 34,201.3 / 29,205 | 3 / 3 | 0 | 7,000.227 | 6,999.934 |
| continuously ready, trace off | 34,267.7 / 29,218 | 3 / 3 | 0 | 7,000.255 | unavailable |
| mixed ready, trace on | 35,912.7 / 30,141.3 | 3 / 3 | 0 | 7,000.233 | 6,999.932 |
| burst 32, trace on | 31,992.7 / 29,699.3 | 0 / 3 | 16,159 | 7,000.006 | 6,999.916 |
| burst 64, trace on | 34,920 / 29,692 | 3 / 3 | 240,024 | 7,000.245 | 6,999.906 |
| slow observer 64, trace on | 2,321.3 / 2,292.3 | 3 / 3 | 431,823 | 7,000.542 | 7,000.210 |

Ready and mixed runs had no source waits and kept the worker selected for
approximately the whole run. CPU1 idle counts were only 0–2 in these cases;
small nonzero counts did not prevent a watchdog timeout. CPU0 still made idle
progress, with maximum gaps about 17.3 ms. The ready source models replenishment
at consumption, so zero queue drops there does not imply supported arrival load.
The overloaded burst/slow cases filled the bounded queue to 128.

Burst 32 passed watchdog/drop checks only with optional timing off: its maximum
CPU1 idle gap/residency was 19.359/18.359 ms and backlog 83. With timing on it
failed all three runs. It is therefore excluded from the envelope that must
hold in both modes. No scheduling fix is implied by this characterization.

Trace-on ready mean throughput was about 0.19%/0.04% below trace-off in timing
modes 0/1; at burst 16 the paired means were identical at reported precision.
These samples characterize measurement impact, not a universal overhead bound.
Optional cost timing reduced ready throughput about 14.6% and changed burst 32's
outcome; comparisons for #133 must keep both instrumentation settings identical.

Receive, processor and observer costs are wall durations including preemption
and ISR time. In ready-on timing mode 1, mean per-call receive/processor/frame
observer/diagnostic costs were 5.034/12.446/0.997/1.161 µs; processor maximum was
14.971 ms; watchdog ISR/backtrace output also contributes to these wall costs.
At burst 16, source waits averaged 905.357 µs; at slow observer 2, callback means
were 201.063/201.147 µs. These are not pure decoder CPU costs. Optional timing
off leaves these cost/span fields unavailable while retaining aggregate counters.

Maximum stop latency across the entire corpus was 1.034 ms, including waiting
cancellation and slow observers. This is an observed bound for these runs,
not a production guarantee. The watchdog failures and FPU pinning provide a
concrete unchanged-runtime baseline for #133. The generic acquisition baseline
can also inform [controller #210](https://github.com/Yuke-hd/mazda-can-accessory-controller/issues/210)
without establishing controller decoder, CAN driver or physical-bus behavior.

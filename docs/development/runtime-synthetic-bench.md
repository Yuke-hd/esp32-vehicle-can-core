# Synthetic task residency and watchdog stress (#132)

This extends the [runtime baseline](runtime-synthetic-baseline.md) with an
optional ESP32 scheduler measurement and configurable stress duration/load.
Production Runtime scheduling and watchdog configuration are unchanged. The
software target has no CAN component or pins. Physical execution requires an
explicitly authorized isolated USB-powered board with CAN-H/CAN-L disconnected.
The current measurements below must come from that bench, not host inference.

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
require no watchdog events, progress on both idle cores, acceptable idle gaps,
queue drops/backlog, stop latency and trace residency. Report mean/range/sample
standard deviation for repeated aggregates. Source arrivals are modeled at
receive checkpoints; this does not establish an actual CAN driver load envelope
or vehicle readiness. Keep observed overload failures in the report rather
than dropping them from the supported-range decision.

## Evidence status

Trace/stress fixtures are host-tested. Actual authorized board aggregates,
watchdog observations and the supported synthetic load range are pending;
no results are inferred from a successful compile or from host timing.

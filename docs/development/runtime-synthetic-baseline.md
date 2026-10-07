# Synthetic runtime baseline (#132)

This additive fixture exercises the current `vehicle_telemetry::Runtime` with
software-generated frames. It has no CAN driver, pins, board binding, Mazda
payloads, capture data, or production scheduling changes. The processor performs
representative single-precision arithmetic, not a decoder. Its malformed frames
use an intentionally invalid DLC to exercise a generic processor result.

The fixtures and build checks are a baseline for #133. The follow-up
[authorized ESP32 bench evidence](runtime-synthetic-bench.md) records actual
post-FPU core affinity, task residency, idle progress, watchdog outcomes and
specific stable/overloaded synthetic loads. Host measurements below remain
exploratory; building firmware alone supplies no hardware evidence. Physical
execution requires separate authorization and an isolated board.

## Host validation and exploratory measurements

Use the [supported host build](supported-build.md) and its ordinary checks:

```sh
cmake -S . -B /tmp/runtime-baseline-host -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build /tmp/runtime-baseline-host --parallel
ctest --test-dir /tmp/runtime-baseline-host --output-on-failure
/tmp/runtime-baseline-host/tests/host/runtime_synthetic_baseline > /tmp/runtime-baseline.csv
```

The six deterministic tests use the real Runtime worker with synthetic clock
costs and explicit gates. They characterize bounded queue conservation and
overflow, mixed status costs, source wait requests, cancellation while waiting,
slow-observer cancellation, and timing-disabled workload counters. Gate timeouts
only detect a test that cannot make progress; no wall-time throughput or OS
fairness threshold is asserted. Synthetic callback costs are not measured CPU
costs. Stop during an observer waits for that frame's callbacks to finish and
prevents the next frame from starting.

The runnable executable collects real host wall times. It runs every scenario
three times in each timing mode for 250 ms, with diagnostics enabled in both
callback kinds throughout. These exploratory host results are not transferable
to FreeRTOS. The default frame cap is UINT32_MAX; a run reaching its cap parks
until cancellation and is invalid as continuous-load evidence.

## Exploratory host sample

One macOS arm64 Release run with Apple LLVM 17.0.0 (clang-1700.0.13.5),
250 ms per repetition, three repetitions per mode, 1 ms receive timeout and
both diagnostic callbacks produced the following sanitized aggregates. No
extra compiler flags were supplied. These are observations from one host,
not acceptance thresholds or evidence of ESP32 scheduling.

| Scenario | Timing | Mean frames/s | Min–max frames/s | Sample SD | Mean/max stop µs |
| --- | --- | --- | --- | --- | --- |
| ready | 0 | 99,657.0 | 99,494–99,755 | 142.1 | 24.7 / 28 |
| ready | 1 | 99,810.7 | 99,779–99,845 | 33.1 | 20.0 / 25 |
| mixed | 0 | 99,884.0 | 99,841–99,913 | 38.0 | 18.0 / 19 |
| mixed | 1 | 99,901.0 | 99,897–99,909 | 6.9 | 17.7 / 22 |
| burst | 0 | 63,874.7 | 63,742–64,052 | 159.8 | 28.3 / 43 |
| burst | 1 | 63,817.0 | 63,725–63,984 | 144.9 | 41.0 / 63 |

Slow-observer throughput was 2,438 frames/s in both modes (integer reporting
made sample SD zero); maximum stop latency was 416/411 µs for modes 0/1.
Cancellation while waiting completed no frames, with maximum stop latencies
72/85 µs. All 30 rows conserved queue counts, stayed within capacity, matched
diagnostic result counts and avoided the frame cap. Ready/mixed rows made no
source wait requests. Timing-mode differences at this scale do not establish
an optimization benefit. Actual uninterrupted CPU execution was not measured.

## ESP32 synthetic target (compile only)

The separate software-only target directly builds the current runtime source
without the `can_bus` component. The pinned firmware CI job compiles it alongside
the retained ACK bench target with ESP-IDF 5.5.4:

```sh
. /Users/yuke/ESP-IDF/v5.5.4/export.sh
python3 tools/check_toolchain.py --scope firmware
idf.py -C firmware/runtime-synthetic-baseline -B /tmp/runtime-baseline-esp32 set-target esp32
idf.py -C firmware/runtime-synthetic-baseline -B /tmp/runtime-baseline-esp32 build
```

Use an unmodified ESP-IDF v5.5.4 checkout for comparison. The defaults select
ESP32 dual-core, 240 MHz, a 1 kHz tick, performance optimization, and task watchdog
monitoring of both idle tasks. Record the resolved sdkconfig; defaults are not
proof of the resolved settings. No watchdog is suppressed by the fixture.

The runtime worker retains production priority idle+1, unpinned task creation,
and its 4096-byte stack. After float arithmetic the fixture samples both the
current executing CPU (`xPortGetCoreID`) and task core affinity
(`xTaskGetCoreID(nullptr)`); ESP-IDF's documented float auto-pinning makes this
sampling necessary. CPU 0 and CPU 1 idle hooks collect counts and maximum wall
gaps, including the measurement's start/end boundaries. The software controller
runs at idle+3 to request cancellation after 250 ms and sleeps 100 ms between
runs. That controller and recovery pause are benchmark behavior and affect
scheduler opportunities. Idle callbacks and their short spinlock remain enabled
in both timing modes. Aggregates are read only after a successful Runtime stop;
failed starts/stops produce `baseline_error` and no valid aggregate row.

## Interpreting the aggregate CSV

Only a metadata line, header, and one `baseline` row per run are printed. There
is no per-frame logging or allocation. The queue holds at most 128 sequence
numbers; catch-up accepts at most 128 entries and counts excess arrivals as drops.
Time-driven burst arrivals are modeled when receive next runs, rather than by an
independent producer task. They expose synthetic acquisition pressure; they do
not represent TWAI interrupt/driver buffering costs.

| Fields | Meaning and limitations |
| --- | --- |
| `receive_*`, `wait_*` | Total/max wall duration of source receive calls and requested source waits; receive includes source queue/instrumentation overhead. Waits may resume early on cancellation. ESP waits round up to ticks. |
| `process_*` | Wrapper wall cost including float work, synthetic busy work, and core sampling. |
| `frame_observer_*`, `diagnostic_*` | Separate costs for both callbacks; diagnostics copy the real Runtime snapshot. Slow-observer work is applied to each callback. |
| `frames_per_second` | All completed processor results divided by start-through-stop wall time, including startup, waits and cancellation. |
| `max_frames_between_waits` | Maximum processed/ignored/malformed results before a source wait request, independent of OS preemption. |
| `max_checkpoint_wall_span_us` | Largest sampled span from first receive or a wait return through callback completion/next wait request. Includes runtime diagnostic work and possible preemption; excludes source waiting and final join. **Not uninterrupted CPU execution.** |
| `stop_us` | Controller wall time around Runtime stop, including source cancellation and worker join. |
| `generated`, `consumed`, `dropped`, `backlog`, `max_backlog` | Synthetic queue pressure; generated = consumed + dropped + final backlog. Consumed may exceed completed results if cancellation discards a just-received frame. |
| `diagnostic_*` result counts | Last observer snapshot; successful rows should match completed result counts. |
| `running_core*`, `affinity_core*`, `idle*`, `max_idle_gap*` | ESP-only samples. Host zeros mean unavailable, not evidence of missing progress. |

Timing mode 0 disables optional cost/segment timestamp aggregation. Queue,
result, checkpoint and core counters, workload/arrival clocks, diagnostics and
idle hooks remain. It is a comparison of optional instrumentation overhead,
not a fully uninstrumented production runtime. Timing-disabled cost columns and
checkpoint spans are unavailable zeros. When timing is enabled, `receive_calls`
includes cancellation receives, while `process_calls` includes only completed
processor invocations.

For a future authorized bench, retain only sanitized aggregate rows and record
source revision, binary hash, ESP-IDF/compiler version, resolved sdkconfig,
optimization, CPU/tick frequency, board/chip revision, watchdog settings,
diagnostic mode and identical workload settings. Compare repetitions by
scenario and timing mode; report mean, min/max and sample standard deviation for
throughput, stop latency, checkpoint spans and idle gaps. Run enough repetitions
to expose variance rather than selecting the best result. Startup/stop are in
each run; inter-run recovery is outside it. Do not combine different settings.

A nonzero idle count alone does not establish acceptable scheduling. Report
per-core maximum gaps, actual post-FPU affinity, drops/backlog, watchdog/reset
outcomes and the exact tested load range. The original fixture cannot infer
uninterrupted CPU execution from wall spans.

The follow-up [task residency and watchdog stress evidence](runtime-synthetic-bench.md)
adds an optional dispatcher wrapper and configurable longer/load-specific runs.
Its measured task residency includes ISR overhead and is separate from the
original source-wait wall span. The authorized corpus supplies an unchanged
runtime baseline for #133, including observed overload failures; it does not
establish a production CAN load envelope or vehicle readiness.

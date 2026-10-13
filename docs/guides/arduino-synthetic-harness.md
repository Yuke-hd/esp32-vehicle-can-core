# Arduino synthetic Runtime harness

[`RuntimeSyntheticHarness`](../../examples/arduino/RuntimeSyntheticHarness/RuntimeSyntheticHarness.ino)
runs the installed generic package with an injected software source, processor
and synchronous observer. It never initializes TWAI, provides a driver binding,
selects CAN pins, or receives physical CAN. Run it on an isolated ESP32 with no
CAN connected. Serial output contains only test names and aggregate counters.
The receive-only normal sketch and notification composition are described in
[the sketch guide](arduino-sketches.md).

## Build and run

Use the exact CLI/core/FQBN/profile and clean staging/install commands in
[the package guide](arduino-package.md), selecting the integrated source SHA.
Copy the sketch outside the checkout, then generate the compiler response file
from the profile (it is build input, not a public C++ header):

```sh
mkdir -p "$package_root/sketches/RuntimeSyntheticHarness"
cp examples/arduino/RuntimeSyntheticHarness/*.ino "$package_root/sketches/RuntimeSyntheticHarness/"
python3 - "$package_root/sketches/RuntimeSyntheticHarness/build_opt.h" <<'PY'
import json, pathlib, sys
profile = json.load(open("tools/arduino/profile.json"))
pathlib.Path(sys.argv[1]).write_text(profile["language_option"] + "\n")
PY
fqbn=$(python3 -c 'import json; print(json.load(open("tools/arduino/profile.json"))["fqbn"])')
"$cli" --config-file "$package_root/config.yaml" compile --verbose \
  --fqbn "$fqbn" --build-path "$package_root/harness-build" \
  "$package_root/sketches/RuntimeSyntheticHarness" > "$package_root/harness-compile.log" 2>&1
```

The documented macOS ARM64 ctags helper override also applies. Compilation is
build evidence only. Before an authorized upload, confirm the connected chip
matches the profile's classic ESP32 target and keep CAN disconnected. Use the
compiled build directory to upload with the same FQBN, then monitor at 115200
baud and reset. The sketch runs once from `loop()` after checked configuration
in `setup()`. No commands or runtime pump are required. Reset to repeat.

## Observable criteria

Each executed check prints `SYNTHETIC <id> PASS|FAIL` with lifecycle, transport,
receive-call count, completed observer count, maximum measured observer duration
in microseconds, and fairness-pause count. Enum mappings are in the installed
public headers. Polls have a 500 ms deadline; ordinary synthetic receives wait
at most 5 ms. The delayed observer requests a finite 35 ms delay. Public ESP
stop waits at most 1000 one-tick iterations after source cleanup succeeds.
These are test deadlines, not hard real-time scheduling guarantees.

| Check | Pass condition |
| --- | --- |
| `setup_configure` | Initial stopped configuration succeeds. |
| `invalid_configuration` | Zero receive timeout is rejected. |
| `start_failure` | Injected source start failure is reported and checked cleanup completes. |
| `initial_silence` | TimedOut transport with no last frame and at least one diagnostic callback. |
| `frame_then_silence` | One synthetic frame completes, then transport times out with last-frame visibility. |
| `delayed_loop_bounded_observer` | Worker completes more than 16 callbacks during an 80 ms loop delay and selects at least one budget pause. |
| `configure_running_rejected` | Configuration while Running returns InvalidState. |
| `observer_cooperative_nonpreemption` | During a 5 ms sample inside a delayed callback, receive/completion/pause counts remain unchanged; later the callback completes, measures at least 25 ms, and a budget pause appears. |
| `receive_fault` | Injected receive Fault produces Faulted lifecycle and transport. |
| `processor_fault` | Injected processor Fault produces Faulted lifecycle with one processor fault. |
| `failed_stop_retains_context` | Source cancellation plus retained cleanup reports Timeout/Stopping and rejects restart with Stopping. |
| `stop_retry_quiescence` | Checked retry completes with exactly one stop and one retry; callback and receive counters remain unchanged for 20 ms afterward. |
| `restart_after_quiescence` | A new run completes a callback and checked stop succeeds. |
| `final_cleanup` | Final checked stop leaves Stopped. |

Acceptance requires all 14 named checks and
`SYNTHETIC SUMMARY checks=14 expected=14 failures=0 result=PASS`.
A failed preparation aborts remaining cases, so a lower check count is FAIL,
never an implicit pass. A missing summary or reset is also not a pass. A failed
cleanup retains the static contexts and ends testing; it never mutates borrowed
worker state or attempts restart without checked quiescence. The application
remains in a delayed idle loop. This harness injects failures in its own source
and processor; it does not inject faults into a physical controller.

## Ownership and synchronization

The static source, processor and observer are constructed before Runtime and
outlive it. Configuration fields are changed only after successful stop and
Stopped lifecycle. Worker-owned state is reset only after that handoff.
Counters and callback-active state shared with the Arduino application task
are atomic; Runtime diagnostic snapshots use its public synchronized API.
Only the Arduino application task calls lifecycle methods. These choices
establish this sketch's synchronization, not arbitrary sketch thread safety.

Observers execute synchronously on Runtime's worker and can delay processing.
The bounded observer performs atomic updates and a measured timestamp read;
the deliberately slow variant requests a finite delay. A cooperative work
budget cannot preempt either processor or observer call. The slow test measures
that limitation, not an independent callback dispatcher or a no-block promise.
Runtime defaults remain 16 frames/2 ms per batch, 512 receive returns/20 ms
runnable work, and a requested 1 ms pause rounded up to at least one tick.
The harness changes only receive polling to 5 ms and silence timeout to 20 ms.
Actual delay depends on scheduling. `work_budget_pauses` counts selected pause
attempts and does not prove a kernel context switch.

## Evidence boundary

Installed-package compile/link, host regression validation, and an uploaded
on-device synthetic run are distinct evidence. A synthetic pass does not
measure actual TWAI receive, bus load, loss/overflow, electrical listen-only
behavior, watchdog margin, or resource coexistence. All physical case rows
under #97 remain separate and require their own measurements. Do not close #97
or the Arduino milestone using this harness output.

# Proposed 0.2.0 release: bounded Runtime work

Status: prepared for review. The release/tag is not published. After required
CI, isolated evidence and independent review, the owner must authorize merge
for publication against the resulting `main` commit. Use tag `0.2.0`, following
the repository's existing `0.1.0` naming. The `vehicle_telemetry` component
manifest is 0.2.0; the unchanged `vehicle_core` component remains 0.1.0. Do not tag an unmerged implementation
branch or substitute the branch head for the final squash-merge commit.

## Changes

Runtime now separates finite processing batches from accumulated runnable-burst
limits. Default controls are 16 frames/2 ms per batch, 512 receive calls/20 ms
per burst, and a requested 1 ms blocking pause. Batch checkpoints preserve the
runnable budget; immediate receive timeouts count toward it. ESP uses positive
cancellation-checked tick delays, allowing lower-priority idle progress without
changing task priority, core affinity or watchdog settings.

Configuration validates all five controls. Fairness uses platform monotonic
time independently of the injected liveness clock. `work_budget_pauses` records
bounded pause attempts. Processing remains single-owner with the same processor,
frame-observer and diagnostic-observer order. Terminal faults precede blocking.
No per-frame allocation, application policy, Mazda payload or extra task is added.

## Compatibility and rollout

Existing two-field RuntimeConfig aggregate initialization remains valid; the
new controls apply finite defaults. Sustained throughput and scheduling behavior
change deliberately. Consumers must rebuild the core and all dependents because
configuration/diagnostic layouts gained fields. No binary ABI compatibility is
claimed. For configuration limits and cooperative callback/stop bounds, read
[Runtime work budgets](../development/runtime-work-budgets.md).

After publication, record the full reviewed merge commit in release notes and
pin that exact commit in the consumer's host FetchContent `GIT_TAG` and all
relevant ESP-IDF Component Manager `version` entries. A floating `main` or
implementation branch is unsuitable. Rebuild the consumer from fresh build and
managed dependency state, run its host tests and generic callback/liveness
contracts, and separately validate its actual decoder/driver workload before
claiming vehicle readiness. Controller-specific updates remain controller-owned.

## Validation and limits

The implementation passes 55 host CTests and 11 Python tooling tests; exact
boundary, predicate and wakeup mutations are detected. Authorized ESP evidence
contains 108 candidate runs plus six paired baseline runs, with no candidate
watchdog events and both-core idle progress. The 1,000 Hz sampled 16,000 arrivals/s
point is retained, while 100 Hz supports the tested 4,000 point and overflows at
16,000. Saturated ready throughput is about 12.0%/7.8% lower than the paired
baseline in timing modes 0/1. See the [complete settings and results](../development/runtime-work-budgets.md).
Clean ESP-IDF 5.5.4 builds, exact final-head CI and independent review remain
release gates; final PR validation records their completion.

The synthetic bench has no CAN driver or physical-bus workload. Budgets are
cooperative and cannot preempt a dependency that fails to return. Tick rate and
optional instrumentation change capacity; supported observations must name the
exact sampled settings. No fairness/response SLA or vehicle readiness is claimed.
No secrets, raw serial logs, vehicle captures, device identifiers or trip data
are included. Revert the Runtime budget change to roll back; preserve #132's
baseline for comparison.

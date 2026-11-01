# Proposed 0.3.0 release: notification channels removed

Status: prepared for review. The release/tag is not published. After required
CI and independent review, the owner must authorize merge for publication
against the resulting `main` commit. Use tag `0.3.0`, following the existing
`0.1.0`/`0.2.0`/`0.2.1` naming. Existing tags, including `0.2.1`, are not
rewritten or replaced. Starting with this release, the `vehicle_core` and
`vehicle_telemetry` component manifests and the Arduino library version follow
the repository release version in lockstep, so all three are 0.3.0. Earlier
releases versioned components independently (at `0.2.1`, `vehicle_core` was
0.1.0 and `vehicle_telemetry` 0.2.0). Do not tag an unmerged branch or substitute the branch head for the final
squash-merge commit.

## Changes

Decoded-signal notification ownership moves above CAN acquisition. This package
no longer ships a notification implementation or notification-only API:

- `vehicle_core/notification_channel.hpp` is removed: `NotificationChannel`,
  `NotificationHandle`, `NotificationStatus`, `NotificationSubscriptionResult`,
  `NotificationStatusResult` and `kNotificationSubscribersPerChannel`.
- `vehicle_core/notification.hpp` is removed, together with
  `vehicle_core::Notification<T>` and `vehicle_core::Callback<T>` from
  `vehicle_core/telemetry_contracts.hpp`.
- The `vehicle_core_notifications` CMake interface target, its
  `VEHICLE_CORE_BUILD_TESTS` option and its test executable are removed.
- The Arduino umbrella `ESP32VehicleCANCore.h` no longer includes the channel.
  The `ReceiveOnlyRuntime` example and the package probe use the Runtime
  observer hooks and generic `Reading` directly.
- The architecture gate now rejects any active code/build reference to the
  removed API.

Preserved unchanged: `Reading`, `Availability`, `ValidationStatus`, frames,
signals, health, time/clocks and decoder contracts, and the
`vehicle_telemetry::Observer::on_frame_processed` and `on_diagnostics` runtime
integration hooks. CAN reception, transport diagnostics, worker lifecycle,
supported platforms and runtime behavior do not change.

## Compatibility and migration

This is a source-breaking public API removal; pre-1.0 versioning signals it with
the minor bump. Consumers that include the removed headers, name the removed
types or link `vehicle_core_notifications` must move that latest-state delivery
into their own application layer, typically fed from a `FrameProcessor` or the
`Observer::on_frame_processed` hook. Consumers that use only frames, readings,
availability and Runtime are unaffected apart from the version pin. No binary
ABI compatibility is claimed; rebuild every dependent.

After publication, pin the reviewed merge commit in the consumer's host
FetchContent `GIT_TAG` and all ESP-IDF Component Manager `version` entries,
rebuild from fresh build and managed dependency state, and run the consumer's
host tests. The downstream migration is tracked in
Yuke-hd/mazda-can-accessory-controller#390.

## Validation and limits

Host CTests, ASan/UBSan host CTests and the Python tooling tests pass; the
Arduino staging test confirms the removed headers are absent from the package.
ESP-IDF firmware and installed Arduino compile/link builds run in required CI.
No hardware or vehicle validation is required or claimed. No secrets, raw logs,
vehicle captures, device identifiers or trip data are included. Revert this
change to restore the 0.2.1 API.

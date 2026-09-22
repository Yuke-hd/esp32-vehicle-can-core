# MCAN-7 receive-only acquisition

The source repository owns the shared receive-only `can_bus` component, its
generic `vehicle_telemetry::CanBusSource` binding, and the isolated bench
adapter. It does not own a vehicle listen-only application, make/model
decoder, or board product. Those artifacts live in a downstream controller
repository that consumes these components as pinned Git dependencies.

## Safety boundary

The public can_bus header exposes lifecycle, receive, statistics, and
configuration operations only. Its ESP-IDF implementation uses
TWAI_MODE_LISTEN_ONLY for the receive-only binding supplied by downstream
applications and has no data-frame transmit API. The separate
`tcan485-bench-ack-only` project selects `bench_can_ack` and TWAI normal mode only
on an isolated, protected classic-CAN bench so a compliant frame can be
acknowledged. It must never be connected to a vehicle.

The allowed nominal bitrates are 125 kbit/s, 250 kbit/s, 500 kbit/s, and
1 Mbit/s. Invalid configurations fail before driver installation. The receive
engine has no mode selector that could turn a vehicle path into normal mode.

## Statistics semantics

A dedicated receive task obtains a bounded poll interval and copies timestamp,
bus number, identifier format, RTR flag, DLC, identifier, and fixed eight-byte
payload into a RawCanFrame value. The task is the only producer for a fixed
capacity 64-frame SPSC ring. One consumer owns public receive calls; callers
must serialize those calls and stop that consumer before restarting
acquisition. The ring allocates no memory after static initialization.

A full ring uses drop-newest semantics: the arriving frame is discarded,
existing FIFO order is preserved, and frames_dropped and queue_overflows
increment. Ring push never waits for a consumer. Driver RX misses and
application-ring drops are reported separately.

Transport and semantic timestamps remain independent. A successfully acquired
frame proves transport liveness, while the source observation timestamp
controls decoder ordering. Equal or older observations cannot extend, clear,
or recover semantic state.

## Validation

Host tests cover bitrate rejection, frame fidelity, FIFO order, drop-newest
behavior, overflow and watermark accounting, controller-reset and bus-off
counters, statistics reset, and producer calls with no consumer. The
architecture gate builds the portable core in isolation, compiles the real
bench adapter test, and checks that the retired capture product has no active
code or build dependency.

These checks are software evidence only. They do not establish ESP-IDF
support, physical bench acceptance, or vehicle safety. No raw vehicle
captures, VIN, credentials, precise location, or reconstructable trip data may
be included in build evidence, Issues, PRs, or releases.

# Telemetry contracts

Stage 0-A freezes the value and lifecycle interfaces used by background
telemetry service work. The declarations are compile-only seams: they do not
start tasks, touch CAN, or provide a concrete hardware product.

## Contract locations

- vehicle_core owns portable frame, signal, time, reading, notification, and
  telemetry contracts.
- mazda owns Mazda value types, freshness/availability, façade contracts, and
  explicit internal decoder/service handoffs.
- components/vehicle_telemetry/include/mazda/vehicle_telemetry.hpp declares
  the non-copyable façade and fixed polling/notification channels.
- vehicle_telemetry retains a generic private lighting sink hook and
  mazda::LightingUpdate for downstream consumers. Concrete accessory policy,
  renderer, and board bindings are not part of this repository.

The façade has fixed subscriber slots per notification channel. Configuration
and subscription mutation are stopped-only operations. Callback values are
copied, callback contexts remain borrowed until stop succeeds, and runtime task
and driver ownership stays private to the background service.

## Boundary checks

The public_header_boundary checker compiles application-facing headers in
isolation, reads compiler dependency files for forbidden transitive headers,
checks the exported vehicle_telemetry_contracts consumer interface, and probes
ordinary versus explicitly authorized internal access.

The architecture_contracts gate separately checks the portable core, the
isolated bench adapter, and absence of active retired-capture dependencies.
It no longer invokes validators or build checks for the moved
WeAct/accessory-controller implementation. That implementation is maintained
in the [dedicated accessory repository](https://github.com/Yuke-hd/mazda-can-accessory-controller).

## Fresh host validation

From a fresh build directory:

    cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
    cmake --build build/host --parallel
    ctest --test-dir build/host --output-on-failure

These checks provide software evidence only. They do not establish ESP-IDF
support, physical bench acceptance, vehicle safety, or concrete lighting
behavior. No credentials, private captures, VIN, precise location, or personal
trip data belong in these artifacts.

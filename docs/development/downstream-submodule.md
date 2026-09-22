# Downstream controller integration

`esp32-vehicle-can-core` is consumed by controller repositories as a pinned
Git submodule. The core repository owns portable CAN/telemetry primitives,
ESP-IDF receive transport, and the explicitly isolated bench target. A
controller repository owns its make/model decoder, telemetry service, lighting
policy, board support, firmware, captures, and protocol evidence.

## Recommended layout

```text
controller-repository/
  third_party/esp32-vehicle-can-core/  # pinned commit
  lib/<make>/                           # protocol and decoder ownership
  components/<controller-service>/     # application composition
```

Add the dependency from the controller repository:

```sh
git submodule add https://github.com/Yuke-hd/mazda-can-telemetry.git \
  third_party/esp32-vehicle-can-core
git -C third_party/esp32-vehicle-can-core checkout <reviewed-core-commit>
git add .gitmodules third_party/esp32-vehicle-can-core
git commit -m "build: pin esp32-vehicle-can-core"
```

The URL remains the migration origin while the GitHub repository is renamed to
`esp32-vehicle-can-core`. Consumers should pin a commit, not a moving branch,
and update it through a dedicated review that runs both the core host tests and
the controller's decoder/service tests.

## Build integration

For a host CMake build, add the submodule's `lib/vehicle_core` directory and
link the `vehicle_core` target. For ESP-IDF, include the core's component
directories through the consuming project's `EXTRA_COMPONENT_DIRS`:

```cmake
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/third_party/esp32-vehicle-can-core/components/can_bus"
    "${CMAKE_CURRENT_LIST_DIR}/third_party/esp32-vehicle-can-core/lib/vehicle_core"
    "${EXTRA_COMPONENT_DIRS}")
```

The controller must keep its decoder and product targets outside the core
submodule. Do not add a make/model include path, DBC, board binding, or
application policy to `esp32-vehicle-can-core` to satisfy a controller build.

## Updating the pin

```sh
git -C third_party/esp32-vehicle-can-core fetch --tags origin
git -C third_party/esp32-vehicle-can-core checkout <new-reviewed-core-commit>
git add third_party/esp32-vehicle-can-core
git commit -m "build: update esp32-vehicle-can-core"
```

Record the selected core commit in the controller change and verify the core's
CI-equivalent host suite. A core update does not authorize a protocol or
vehicle-data change; those remain controller-repository responsibilities.

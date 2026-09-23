# Consumer integration

`esp32-vehicle-can-core` is consumed by controller repositories without
copying any component into both repositories. A controller owns its make/model
decoder, telemetry publication/descriptors/subscriptions, lighting policy,
board support, firmware, captures, and protocol evidence. This repository owns
only the generic `vehicle_core`, `vehicle_telemetry`, `can_bus`, and isolated
bench components.

## Host CMake with FetchContent

Pin a reviewed core commit and fetch the component source directory directly:

```cmake
include(FetchContent)

FetchContent_Declare(
  vehicle_can_core
  GIT_REPOSITORY https://github.com/Yuke-hd/esp32-vehicle-can-core.git
  GIT_TAG <reviewed-core-commit>
  SOURCE_SUBDIR components/vehicle_core
)
FetchContent_MakeAvailable(vehicle_can_core)

target_link_libraries(my_controller PRIVATE vehicle_core)
```

The fetched component exposes the ordinary-CMake `vehicle_core` target and does
not configure this repository's host tests. A controller may
fetch `components/vehicle_telemetry` separately with the same pattern if it
needs the generic runtime.

## ESP-IDF Component Manager Git dependency

Declare the pinned component in the consuming component's `idf_component.yml`:

```yaml
dependencies:
  vehicle_core:
    git: https://github.com/Yuke-hd/esp32-vehicle-can-core.git
    path: components/vehicle_core
    version: "<reviewed-core-commit>"
```

If the controller also uses
the generic runtime or receive transport, declare `vehicle_telemetry` and
`can_bus` as separate Git dependencies with the same reviewed repository
commit and paths. Do not add copied component directories under the controller
repository's `components/` tree.

The generic runtime receives injected source, processor, and observer objects;
the controller's `mazda_telemetry` component implements those interfaces and
owns all Mazda-specific state and publication contracts. No generic component
should include a make/model header or name a signal ID.

The optional `vehicle_telemetry::CanBusSource` adapter is owned by this core
repository. It is the ESP-IDF binding from `can_bus` to the injected
`AcquisitionSource` contract; a controller may replace it with a different
source without changing `Runtime`. This keeps the dependency direction
`vehicle_telemetry -> can_bus -> vehicle_core` and avoids a controller/core
component cycle.

## Updating the dependency

Review the core commit, update the host FetchContent `GIT_TAG` and/or the
Component Manager `version`, then run both repositories' host tests. A core
update does not authorize a protocol or vehicle-data change; those remain
controller-repository responsibilities.

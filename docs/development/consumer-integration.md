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

The fetched component exposes the ordinary-CMake `vehicle_core` target. Its
own `vehicle_core_notification_tests` target is disabled by default, even when
the consumer sets CTest's `BUILD_TESTING=ON`. Set
`VEHICLE_CORE_BUILD_TESTS=ON` before `FetchContent_MakeAvailable` to opt in to
that target; it also requires `Threads::Threads`. The repository root defaults
this component option to its host-test setting, so root host CI continues to
build and register the test. A controller may fetch
`components/vehicle_telemetry` separately with the same pattern if it needs the
generic runtime.

## ESP-IDF Component Manager Git dependency

Declare the pinned components in the consuming component's `idf_component.yml`.
`can_bus` lists `vehicle_core` in its CMake requirements but has no Component
Manager manifest of its own, so an app that reads frames directly must declare
both Git dependencies at the same reviewed commit:

```yaml
dependencies:
  vehicle_core:
    git: https://github.com/Yuke-hd/esp32-vehicle-can-core.git
    path: components/vehicle_core
    version: "<reviewed-core-commit>"
  can_bus:
    git: https://github.com/Yuke-hd/esp32-vehicle-can-core.git
    path: components/can_bus
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

### Minimal receive-and-print app

The `can_bus` component needs one application-provided TWAI binding at link
time. Put the binding beside the app in `main/can_bus_binding.cpp`, and compile
both files in `main/CMakeLists.txt`:

```cmake
idf_component_register(
  SRCS "main.cpp" "can_bus_binding.cpp"
  INCLUDE_DIRS "."
  REQUIRES can_bus vehicle_core esp_driver_twai esp_driver_gpio
)
```

This binding example uses GPIO 27 for TX and GPIO 26 for RX on a classic ESP32.
Use pins routed to the CAN transceiver on the actual target board, and perform
any board-specific transceiver power/standby setup before starting acquisition.
Every firmware must provide exactly one binding; it must keep TWAI in
listen-only mode and set the TX queue length to zero:

```cpp
// main/can_bus_binding.cpp
#include "can_bus/driver_binding.hpp"
#include "driver/gpio.h"

namespace can_bus::internal {
void configure_driver(twai_general_config_t &configuration) noexcept {
  configuration = TWAI_GENERAL_CONFIG_DEFAULT(
      GPIO_NUM_27, GPIO_NUM_26, TWAI_MODE_LISTEN_ONLY);
  configuration.tx_queue_len = 0;
}
} // namespace can_bus::internal
```

Then `main/main.cpp` can start acquisition and print basic frame metadata. The
loop below is the only consumer of `receive()`; timeouts are normal when the
bus is idle, while other results trigger cleanup. It intentionally omits
payload logging and vehicle-specific decoding:

```cpp
// main/main.cpp
#include "can_bus/can_bus.h"
#include "esp_log.h"

namespace {
constexpr char kTag[] = "can_reader";
constexpr can_bus::Configuration kConfiguration{500'000, 0};
} // namespace

extern "C" void app_main(void) {
  // Initialize the board's CAN transceiver receive path here if required.
  if (can_bus::start(kConfiguration) != can_bus::Result::kOk) {
    ESP_LOGE(kTag, "CAN receive startup failed");
    return;
  }

  for (;;) {
    vehicle_core::RawCanFrame frame{};
    const can_bus::Result result = can_bus::receive(frame, 1'000);
    if (result == can_bus::Result::kTimeout) {
      continue;
    }
    if (result != can_bus::Result::kOk) {
      ESP_LOGE(kTag, "CAN receive failed (%u)", static_cast<unsigned>(result));
      break;
    }

    ESP_LOGI(kTag, "%s id=0x%lx dlc=%u bus=%u",
             frame.is_extended() ? "extended" : "standard",
             static_cast<unsigned long>(frame.identifier),
             static_cast<unsigned>(frame.dlc), static_cast<unsigned>(frame.bus_id));
  }

  if (can_bus::stop() != can_bus::Result::kOk) {
    ESP_LOGE(kTag, "CAN receive cleanup failed");
  }
}
```

`500'000` is the configured bitrate in bits per second; it must match the bus
and can be changed to a supported rate. The GPIO numbers above are an
ESP32-specific illustration, not universal board wiring. Keep one serialized
consumer for `receive()` calls. For a sustained frame stream, replace per-frame
logging with bounded processing so console output does not become the receive
bottleneck.

## Updating the dependency

Review the core commit, update the host FetchContent `GIT_TAG` and/or the
Component Manager `version`, then run both repositories' host tests. A core
update does not authorize a protocol or vehicle-data change; those remain
controller-repository responsibilities.

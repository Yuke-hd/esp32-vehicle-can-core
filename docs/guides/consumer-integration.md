# Consumer integration

This guide shows how to use the core components in your own project. You end
up with an ESP32 app that receives CAN frames and logs them.

**Your project** (the "controller") owns everything vehicle-specific: the
decoder, signal definitions, what you do with decoded values, board support,
and captures. **This repo** provides only the generic `vehicle_core`,
`can_bus`, `vehicle_telemetry`, and bench components.

Always pull the components in as pinned Git dependencies. Don't copy them into
your project's `components/` folder.

## Contents

- [Choose your setup](#choose-your-setup)
- [ESP-IDF: receive and print frames](#esp-idf-receive-and-print-frames)
- [Using the runtime loop](#using-the-runtime-loop)
- [Host CMake (no ESP-IDF)](#host-cmake-no-esp-idf)
- [Updating the pinned commit](#updating-the-pinned-commit)

## Choose your setup

| You want to... | Declare |
| --- | --- |
| Read raw frames on an ESP32 | `vehicle_core` + `can_bus` |
| Use the runtime loop that feeds your decoder | `vehicle_core` + `can_bus` + `vehicle_telemetry` |
| Use only the shared types, for example in host tests | `vehicle_core` (see [Host CMake](#host-cmake-no-esp-idf)) |

`can_bus` has no Component Manager manifest of its own, so always declare
`vehicle_core` next to it.

## ESP-IDF: receive and print frames

### 1. Add the dependencies

In your component's `idf_component.yml` (usually `main/idf_component.yml`),
pin every entry to the **same** reviewed commit SHA:

```yaml
dependencies:
  vehicle_core:
    git: https://github.com/Yuke-hd/esp32-vehicle-can-core.git
    path: components/vehicle_core
    version: "<commit-sha>"
  can_bus:
    git: https://github.com/Yuke-hd/esp32-vehicle-can-core.git
    path: components/can_bus
    version: "<commit-sha>"
```

### 2. Provide the TWAI pin binding

`can_bus` doesn't know your board's wiring, so your app must provide exactly
one binding function that sets the TX/RX pins. Create
`main/can_bus_binding.cpp`:

```cpp
// main/can_bus_binding.cpp
#include "can_bus/driver_binding.hpp"
#include "driver/gpio.h"

namespace can_bus::internal {
void configure_driver(twai_general_config_t &configuration) noexcept {
  configuration = TWAI_GENERAL_CONFIG_DEFAULT(
      GPIO_NUM_27, GPIO_NUM_26, TWAI_MODE_LISTEN_ONLY);  // TX, RX
  configuration.tx_queue_len = 0;
}
} // namespace can_bus::internal
```

- Replace GPIO 27/26 with the pins wired to your board's CAN transceiver.
- **Keep `TWAI_MODE_LISTEN_ONLY` and `tx_queue_len = 0`.** They keep the ESP32
  from transmitting on the vehicle bus.

### 3. Write the app

Register both files in `main/CMakeLists.txt`:

```cmake
idf_component_register(
  SRCS "main.cpp" "can_bus_binding.cpp"
  INCLUDE_DIRS "."
  REQUIRES can_bus vehicle_core esp_driver_twai esp_driver_gpio
)
```

Then start receiving in `main/main.cpp`:

```cpp
// main/main.cpp
#include "can_bus/can_bus.h"
#include "esp_log.h"

namespace {
constexpr char kTag[] = "can_reader";
constexpr can_bus::Configuration kConfiguration{500'000, 0};  // bitrate, bus id
} // namespace

extern "C" void app_main(void) {
  // Power up or enable your board's CAN transceiver here if it needs it.
  if (can_bus::start(kConfiguration) != can_bus::Result::kOk) {
    ESP_LOGE(kTag, "CAN receive startup failed");
    return;
  }

  for (;;) {
    vehicle_core::RawCanFrame frame{};
    const can_bus::Result result = can_bus::receive(frame, 1'000);
    if (result == can_bus::Result::kTimeout) {
      continue;  // Bus is idle. This is normal.
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

Things to know:

- **Bitrate:** `500'000` bits/s must match your vehicle's bus.
- **One reader:** call `receive()` from a single task only.
- **High traffic:** logging every frame will become the bottleneck on a busy
  bus. Replace the log line with your own lightweight processing.
- The example only logs frame metadata. It doesn't log payloads or decode
  anything, which is your project's job.

## Using the runtime loop

To skip writing the receive loop yourself, add `vehicle_telemetry` at the same
commit:

```yaml
  vehicle_telemetry:
    git: https://github.com/Yuke-hd/esp32-vehicle-can-core.git
    path: components/vehicle_telemetry
    version: "<commit-sha>"
```

`vehicle_telemetry::Runtime` takes three objects that you pass to its
constructor:

| Interface | What it does | Who provides it |
| --- | --- | --- |
| `AcquisitionSource` | Supplies frames | `vehicle_telemetry::CanBusSource` (wraps `can_bus`), or your own |
| `FrameProcessor` | Decodes frames for your vehicle | Your project |
| `Observer` | Receives results and diagnostics | Your project |

Keep vehicle-specific code in your project. Generic components must never
include a make/model header or name a signal ID. Dependencies only point one
way: `vehicle_telemetry → can_bus → vehicle_core`.

To tune how much work the runtime does per cycle, see
[Runtime work budgets](../design/runtime-work-budgets.md).

## Host CMake (no ESP-IDF)

`vehicle_core` is plain C++17, so you can use it in host builds and tests with
FetchContent:

```cmake
include(FetchContent)

FetchContent_Declare(
  vehicle_can_core
  GIT_REPOSITORY https://github.com/Yuke-hd/esp32-vehicle-can-core.git
  GIT_TAG <commit-sha>
  SOURCE_SUBDIR components/vehicle_core
)
FetchContent_MakeAvailable(vehicle_can_core)

target_link_libraries(my_controller PRIVATE vehicle_core)
```

- The component's own tests are **off by default**, even if you set
  `BUILD_TESTING=ON`. To build them, set `VEHICLE_CORE_BUILD_TESTS=ON` before
  `FetchContent_MakeAvailable`. They need `Threads::Threads`.
- To use the runtime on the host, fetch `components/vehicle_telemetry` the same
  way.

## Updating the pinned commit

1. Review the new core commit and its [release notes](../releases).
2. Update the Component Manager `version` and/or the FetchContent `GIT_TAG`.
   Keep all components on the same commit.
3. Run the host tests in both this repo and your project.
4. If you use the runtime, rebuild every component that depends on it. Newer
   versions may add configuration and diagnostic fields; the
   [0.2.0 release notes](../releases/runtime-budgets-0.2.0.md) cover that
   change.

Updating the core never changes your decoder or vehicle data. Those stay in
your project.

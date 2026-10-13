// Private compile/link probe only. Do not upload: pins are placeholders.
#include <Arduino.h>
#include <ESP32VehicleCANCore.h>
#include <can_bus/driver_binding.hpp>
#include <esp_idf_version.h>
#include <vehicle_core/notification.hpp>
#include <vehicle_core/notification_channel.hpp>
#include <vehicle_core/reading.hpp>
#include <vehicle_telemetry/can_bus_source.hpp>
#include <vehicle_telemetry/runtime.hpp>

#ifndef ESP_PLATFORM
#error The installed package must select the ESP_PLATFORM runtime backend
#endif
static_assert(__cplusplus == 201703L, "This probe must compile as C++17");
static_assert(CONFIG_IDF_TARGET_ESP32 == 1, "Only classic ESP32 is selected");
static_assert(ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(5, 5, 4), "Bundled IDF changed");
static_assert(CONFIG_FREERTOS_HZ == 1000, "Selected tick configuration changed");
static_assert(__GNUC__ == 14 && __GNUC_MINOR__ == 2, "Selected compiler changed");
static_assert(CONFIG_COMPILER_CXX_EXCEPTIONS == 1, "Exceptions configuration changed");
static_assert(CONFIG_ESP_INT_WDT_TIMEOUT_MS == 300, "Interrupt watchdog changed");
static_assert(CONFIG_ESP_INT_WDT_CHECK_CPU1 == 1, "CPU1 interrupt watchdog changed");
static_assert(CONFIG_ESP_TASK_WDT_EN == 1, "Task watchdog changed");
#if defined(CONFIG_FREERTOS_UNICORE) || defined(__GXX_RTTI) || !defined(__EXCEPTIONS)
#error Expected dual-core, disabled RTTI and enabled C++ exceptions
#endif
#pragma message("package probe: ESP_PLATFORM, C++17, classic ESP32, IDF 5.5.4, tick 1000 Hz")

namespace can_bus::internal {
void configure_driver(twai_general_config_t &configuration) noexcept {
  configuration = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_21, GPIO_NUM_22, TWAI_MODE_LISTEN_ONLY);
  configuration.tx_queue_len = 0;
}
} // namespace can_bus::internal

namespace {
vehicle_core::NotificationChannel<int, 1> channel;
vehicle_core::Reading<int> observed;
volatile std::uint32_t compile_result;

void on_notice(void *, const vehicle_core::Notification<int> &notice) noexcept {
  observed = notice.current;
}

class Processor final : public vehicle_telemetry::FrameProcessor {
public:
  void reset() noexcept override {}
  vehicle_telemetry::ProcessResult process(const vehicle_core::RawCanFrame &) noexcept override {
    return {vehicle_telemetry::ProcessStatus::Processed};
  }
};

class ProbeObserver final : public vehicle_telemetry::Observer {
public:
  void on_frame_processed(const vehicle_core::RawCanFrame &,
                          const vehicle_telemetry::ProcessResult &) noexcept override {}
  void
  on_diagnostics(const vehicle_telemetry::TransportDiagnostics &diagnostics) noexcept override {
    compile_result = static_cast<std::uint32_t>(diagnostics.lifecycle);
  }
};

vehicle_telemetry::CanBusSource source{{500'000, 0}};
Processor processor;
ProbeObserver observer;
vehicle_telemetry::Runtime runtime{source, processor, observer};
} // namespace

void setup() {
  const auto first = channel.subscribe(on_notice, nullptr);
  if (first.value)
    compile_result = static_cast<std::uint32_t>(channel.unsubscribe(*first.value));
  const auto second = channel.register_callback(on_notice, nullptr);
  compile_result = second.ok();
  compile_result = static_cast<std::uint32_t>(channel.start());
  observed = {42, vehicle_core::Availability::Fresh, vehicle_core::ValidationStatus::Observed};
  compile_result = static_cast<std::uint32_t>(channel.publish(observed));
  compile_result = static_cast<std::uint32_t>(channel.dispatch_one());
  compile_result = channel.dispatch_pending(2);
  compile_result = static_cast<std::uint32_t>(channel.stop());
  compile_result = static_cast<std::uint32_t>(runtime.configure({}).status);
  compile_result = static_cast<std::uint32_t>(runtime.start().status);
  compile_result = static_cast<std::uint32_t>(runtime.lifecycle());
  compile_result = runtime.diagnostics().frames_processed;
  compile_result = static_cast<std::uint32_t>(runtime.stop().status);
}

void loop() { delay(1000); }

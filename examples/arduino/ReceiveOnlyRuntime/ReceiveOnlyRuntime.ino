// Receive-only composition. Set pins/bitrate for your isolated setup before upload.
#include <Arduino.h>
#include <ESP32VehicleCANCore.h>
#include <atomic>
#include <can_bus/driver_binding.hpp>

namespace can_bus::internal {
void configure_driver(twai_general_config_t &configuration) noexcept {
  configuration = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_27, GPIO_NUM_26, TWAI_MODE_LISTEN_ONLY);
  configuration.tx_queue_len = 0;
}
} // namespace can_bus::internal

namespace {
class Processor final : public vehicle_telemetry::FrameProcessor {
public:
  void reset() noexcept override {}
  vehicle_telemetry::ProcessResult process(const vehicle_core::RawCanFrame &) noexcept override {
    return {vehicle_telemetry::ProcessStatus::Processed};
  }
};

class CountObserver final : public vehicle_telemetry::Observer {
public:
  // Application calls this only after Runtime has stopped and its worker is quiescent.
  void reset() noexcept { count_.store(0); }
  void on_frame_processed(const vehicle_core::RawCanFrame &,
                          const vehicle_telemetry::ProcessResult &) noexcept override {
    count_.fetch_add(1, std::memory_order_relaxed);
  }
  void on_diagnostics(const vehicle_telemetry::TransportDiagnostics &) noexcept override {}
  // Generic Reading copy for loop(); signal notification ownership lives above CAN acquisition.
  vehicle_core::Reading<std::uint64_t> count() const noexcept {
    return {count_.load(std::memory_order_relaxed), vehicle_core::Availability::FreshnessUnverified,
            vehicle_core::ValidationStatus::Observed};
  }

private:
  std::atomic<std::uint64_t> count_{0}; // Worker writes; loop() reads.
};

// Construction is inert. Borrowed dependencies outlive Runtime and all callbacks.
vehicle_telemetry::CanBusSource source{{500'000, 0}};
Processor processor;
CountObserver observer;
vehicle_telemetry::Runtime runtime{source, processor, observer};

void report(const char *operation, vehicle_telemetry::StatusResult result) {
  Serial.printf("%s result=%u lifecycle=%u\n", operation, static_cast<unsigned>(result.status),
                static_cast<unsigned>(runtime.lifecycle()));
}

void show_diagnostics() {
  const auto diagnostics = runtime.diagnostics();
  Serial.printf("diagnostics lifecycle=%u transport=%u processed=%llu dropped=%llu "
                "budget_pauses=%llu observed_count=%llu\n",
                static_cast<unsigned>(diagnostics.lifecycle),
                static_cast<unsigned>(diagnostics.transport), diagnostics.frames_processed,
                diagnostics.acquisition.frames_dropped, diagnostics.work_budget_pauses,
                observer.count().value.value_or(0));
}

bool stop_runtime() {
  const auto result = runtime.stop();
  report("stop", result);
  if ((!result.ok() && result.status != vehicle_telemetry::ResultCode::NotRunning) ||
      runtime.lifecycle() != vehicle_telemetry::LifecycleState::Stopped) {
    Serial.println("cleanup incomplete; dependencies retained; send s to retry");
    return false;
  }
  return true;
}

void start_runtime() {
  if (runtime.lifecycle() != vehicle_telemetry::LifecycleState::Stopped) {
    Serial.println("start requires completed stop");
    return;
  }
  const auto configured = runtime.configure({});
  report("configure", configured);
  if (!configured.ok())
    return;
  observer.reset();
  const auto started = runtime.start();
  report("start", started);
  if (!started.ok())
    (void)stop_runtime();
}
} // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("ReceiveOnlyRuntime: s stop/retry, r stop then restart, d diagnostics");
  start_runtime();
}

void loop() {
  if (Serial.available()) {
    const int command = Serial.read();
    if (command == 's')
      (void)stop_runtime();
    else if (command == 'r' && stop_runtime())
      start_runtime();
    else if (command == 'd')
      show_diagnostics();
  }
  static unsigned long previous_report = 0;
  const unsigned long now = millis();
  if (now - previous_report >= 1000) {
    previous_report = now;
    show_diagnostics();
  }
  delay(10);
}

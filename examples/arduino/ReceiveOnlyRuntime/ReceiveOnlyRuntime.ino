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
using CountChannel = vehicle_core::NotificationChannel<std::uint64_t, 1>;
CountChannel channel;
vehicle_core::Reading<std::uint64_t> delivered_count;

void on_count(void *context, const vehicle_core::Notification<std::uint64_t> &notice) noexcept {
  // Explicit dispatch in setup/loop owns this context; the Runtime worker never writes it.
  *static_cast<vehicle_core::Reading<std::uint64_t> *>(context) = notice.current;
}

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
  void reset() noexcept {
    count_ = 0;
    publish_failures.store(0);
  }
  void on_frame_processed(const vehicle_core::RawCanFrame &,
                          const vehicle_telemetry::ProcessResult &) noexcept override {
    const vehicle_core::Reading<std::uint64_t> count{
        ++count_, vehicle_core::Availability::FreshnessUnverified,
        vehicle_core::ValidationStatus::Observed};
    if (channel.publish(count) != vehicle_core::NotificationStatus::Ok)
      publish_failures.fetch_add(1);
  }
  void on_diagnostics(const vehicle_telemetry::TransportDiagnostics &) noexcept override {}
  std::atomic<std::uint32_t> publish_failures{0};

private:
  std::uint64_t count_{0}; // Worker-owned between successful start and stop.
};

// Construction is inert. Borrowed dependencies outlive Runtime and all callbacks.
vehicle_telemetry::CanBusSource source{{500'000, 0}};
Processor processor;
CountObserver observer;
vehicle_telemetry::Runtime runtime{source, processor, observer};
bool registered = false;

void report(const char *operation, vehicle_telemetry::StatusResult result) {
  Serial.printf("%s result=%u lifecycle=%u\n", operation, static_cast<unsigned>(result.status),
                static_cast<unsigned>(runtime.lifecycle()));
}

void show_diagnostics() {
  const auto diagnostics = runtime.diagnostics();
  Serial.printf("diagnostics lifecycle=%u transport=%u processed=%llu dropped=%llu "
                "budget_pauses=%llu delivered_count=%llu publish_failures=%lu\n",
                static_cast<unsigned>(diagnostics.lifecycle),
                static_cast<unsigned>(diagnostics.transport), diagnostics.frames_processed,
                diagnostics.acquisition.frames_dropped, diagnostics.work_budget_pauses,
                delivered_count.value.value_or(0),
                static_cast<unsigned long>(observer.publish_failures.load()));
}

bool stop_runtime() {
  const auto result = runtime.stop();
  report("stop", result);
  if ((!result.ok() && result.status != vehicle_telemetry::ResultCode::NotRunning) ||
      runtime.lifecycle() != vehicle_telemetry::LifecycleState::Stopped) {
    Serial.println("cleanup incomplete; dependencies retained; send s to retry");
    return false;
  }
  // Runtime stop completed before publication/context reset or channel shutdown.
  if (channel.running()) {
    const auto status = channel.stop();
    Serial.printf("channel stop result=%u\n", static_cast<unsigned>(status));
    if (status != vehicle_core::NotificationStatus::Ok)
      return false;
  }
  return true;
}

void start_runtime() {
  if (!registered || runtime.lifecycle() != vehicle_telemetry::LifecycleState::Stopped ||
      channel.running()) {
    Serial.println("start requires successful registration and completed stop");
    return;
  }
  const auto configured = runtime.configure({});
  report("configure", configured);
  if (!configured.ok())
    return;
  observer.reset();
  const auto status = channel.start();
  Serial.printf("channel start result=%u\n", static_cast<unsigned>(status));
  if (status != vehicle_core::NotificationStatus::Ok)
    return;
  const auto initial = channel.dispatch_one();
  Serial.printf("initial dispatch result=%u\n", static_cast<unsigned>(initial));
  if (initial != vehicle_core::NotificationStatus::Ok) {
    (void)stop_runtime();
    return;
  }
  const auto started = runtime.start();
  report("start", started);
  if (!started.ok())
    (void)stop_runtime();
}
} // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("ReceiveOnlyRuntime: s stop/retry, r stop then restart, d diagnostics");
  // Both registration forms are stopped-only. Retain the final callback over restarts.
  const auto temporary = channel.subscribe(on_count, &delivered_count);
  if (!temporary.ok()) {
    Serial.printf("subscribe failed result=%u\n", static_cast<unsigned>(temporary.status));
    return;
  }
  const auto removed = channel.unsubscribe(*temporary.value);
  if (removed != vehicle_core::NotificationStatus::Ok) {
    Serial.printf("unsubscribe failed result=%u\n", static_cast<unsigned>(removed));
    return;
  }
  const auto subscription = channel.register_callback(on_count, &delivered_count);
  registered = subscription.ok();
  Serial.printf("register_callback result=%u\n", static_cast<unsigned>(subscription.status));
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
  // This dispatches application notifications; acquisition runs on Runtime's worker.
  if (channel.running())
    (void)channel.dispatch_pending(2);
  static unsigned long previous_report = 0;
  const unsigned long now = millis();
  if (now - previous_report >= 1000) {
    previous_report = now;
    show_diagnostics();
  }
  delay(10);
}

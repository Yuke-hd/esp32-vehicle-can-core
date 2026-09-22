#include "vehicle_telemetry/runtime.hpp"

#include <atomic>
#include <limits>
#include <mutex>
#include <new>

#if defined(ESP_PLATFORM)
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include <chrono>
#include <thread>
#endif

namespace vehicle_telemetry {
namespace {

constexpr std::uint32_t kMaximumReceiveTimeoutMs = 60'000;
constexpr std::uint32_t kMaximumSilenceTimeoutMs = 300'000;

[[nodiscard]] bool valid_config(const RuntimeConfig &config) noexcept {
  return config.receive_timeout_ms != 0 && config.receive_timeout_ms <= kMaximumReceiveTimeoutMs &&
         config.transport_silence_timeout_ms >= config.receive_timeout_ms &&
         config.transport_silence_timeout_ms <= kMaximumSilenceTimeoutMs;
}

[[nodiscard]] std::uint64_t
milliseconds_to_microseconds(const std::uint32_t milliseconds) noexcept {
  constexpr std::uint64_t kMicrosPerMillisecond = 1'000;
  return static_cast<std::uint64_t>(milliseconds) * kMicrosPerMillisecond;
}

class SystemMonotonicClock final : public vehicle_core::MonotonicClock {
public:
  [[nodiscard]] vehicle_core::MonotonicTimestamp now() const noexcept override {
#if defined(ESP_PLATFORM)
    return static_cast<vehicle_core::MonotonicTimestamp>(esp_timer_get_time());
#else
    const auto duration = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<vehicle_core::MonotonicTimestamp>(
        std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
#endif
  }
};

[[nodiscard]] const vehicle_core::MonotonicClock &system_clock() noexcept {
  static const SystemMonotonicClock clock;
  return clock;
}

class RuntimeImplementation final {
public:
  RuntimeImplementation(AcquisitionSource &source, FrameProcessor &processor, Observer &observer,
                        const vehicle_core::MonotonicClock &clock) noexcept
      : source_(&source), processor_(&processor), observer_(&observer), clock_(&clock) {
    diagnostics_.transport = vehicle_core::TransportHealth::Stopped;
  }

  ~RuntimeImplementation() noexcept { shutdown_noexcept(); }

  [[nodiscard]] StatusResult configure(const RuntimeConfig &config) noexcept {
    if (!valid_config(config))
      return {ResultCode::InvalidConfiguration};
    std::lock_guard<std::mutex> lifecycle_lock{lifecycle_mutex_};
    if (lifecycle_.load(std::memory_order_acquire) != LifecycleState::Stopped)
      return {ResultCode::InvalidState};
    config_ = config;
    return {ResultCode::Ok};
  }

  [[nodiscard]] StatusResult start() noexcept {
    std::lock_guard<std::mutex> lifecycle_lock{lifecycle_mutex_};
    if (!valid_config(config_))
      return {ResultCode::InvalidConfiguration};

    const auto state = lifecycle_.load(std::memory_order_acquire);
    if (state != LifecycleState::Stopped)
      return {lifecycle_result(state)};

    processor_->reset();
    const auto source_result = source_->start();
    if (!source_result.ok()) {
      update_after_start_failure(source_result.status);
      return source_result;
    }
    source_started_ = true;
    source_stop_called_ = false;
    source_stop_result_ = {ResultCode::Ok};

    stop_requested_.store(false, std::memory_order_release);
    worker_active_.store(true, std::memory_order_release);
    {
      std::lock_guard<std::mutex> diagnostics_lock{diagnostics_mutex_};
      diagnostics_ = TransportDiagnostics{};
      diagnostics_.lifecycle = LifecycleState::Running;
      diagnostics_.transport = vehicle_core::TransportHealth::AwaitingTraffic;
      diagnostics_.acquisition = source_->statistics();
    }
    lifecycle_.store(LifecycleState::Running, std::memory_order_release);

#if defined(ESP_PLATFORM)
    const auto created = xTaskCreate(&RuntimeImplementation::task_entry, "vehicle_telemetry", 4096,
                                     this, tskIDLE_PRIORITY + 1, &task_);
    if (created != pdPASS) {
      worker_active_.store(false, std::memory_order_release);
      stop_requested_.store(true, std::memory_order_release);
      (void)stop_source_once();
      lifecycle_.store(LifecycleState::Stopped, std::memory_order_release);
      set_lifecycle_diagnostic(LifecycleState::Stopped, vehicle_core::TransportHealth::Stopped);
      return {ResultCode::Faulted};
    }
#else
    try {
      worker_ = std::thread(&RuntimeImplementation::run_loop, this);
    } catch (...) {
      worker_active_.store(false, std::memory_order_release);
      stop_requested_.store(true, std::memory_order_release);
      (void)stop_source_once();
      lifecycle_.store(LifecycleState::Stopped, std::memory_order_release);
      set_lifecycle_diagnostic(LifecycleState::Stopped, vehicle_core::TransportHealth::Stopped);
      return {ResultCode::Faulted};
    }
#endif
    return {ResultCode::Ok};
  }

  [[nodiscard]] StatusResult stop() noexcept {
#if !defined(ESP_PLATFORM)
    if (worker_.joinable() && worker_.get_id() == std::this_thread::get_id())
      return {ResultCode::InvalidState};
#else
    if (task_ == xTaskGetCurrentTaskHandle())
      return {ResultCode::InvalidState};
#endif

    std::lock_guard<std::mutex> lifecycle_lock{lifecycle_mutex_};
    const auto state = lifecycle_.load(std::memory_order_acquire);
    if (state == LifecycleState::Stopped && !source_started_)
      return {ResultCode::NotRunning};

    if (state != LifecycleState::Stopping)
      lifecycle_.store(LifecycleState::Stopping, std::memory_order_release);
    set_lifecycle_diagnostic(LifecycleState::Stopping, vehicle_core::TransportHealth::Stopped);
    stop_requested_.store(true, std::memory_order_release);

    const auto source_result = stop_source_once();
    if (source_result.status == ResultCode::Timeout || source_result.status == ResultCode::Stopping)
      return {ResultCode::Timeout};

#if !defined(ESP_PLATFORM)
    if (worker_.joinable())
      worker_.join();
#else
    constexpr TickType_t kWaitTicks = pdMS_TO_TICKS(1) == 0 ? 1 : pdMS_TO_TICKS(1);
    constexpr std::uint32_t kMaximumWaitTicks = 1'000;
    std::uint32_t waited = 0;
    while (worker_active_.load(std::memory_order_acquire) && waited < kMaximumWaitTicks) {
      vTaskDelay(kWaitTicks);
      ++waited;
    }
    if (worker_active_.load(std::memory_order_acquire))
      return {ResultCode::Timeout};
#endif

    worker_active_.store(false, std::memory_order_release);
    const auto final_state = source_result.ok() ? LifecycleState::Stopped : LifecycleState::Faulted;
    const auto final_transport = source_result.ok() ? vehicle_core::TransportHealth::Stopped
                                                    : vehicle_core::TransportHealth::Faulted;
    lifecycle_.store(final_state, std::memory_order_release);
    set_lifecycle_diagnostic(final_state, final_transport);
    if (source_result.ok())
      return {ResultCode::Ok};
    return {ResultCode::Faulted};
  }

  [[nodiscard]] LifecycleState lifecycle() const noexcept {
    return lifecycle_.load(std::memory_order_acquire);
  }

  [[nodiscard]] TransportDiagnostics diagnostics() const noexcept {
    std::lock_guard<std::mutex> lock{diagnostics_mutex_};
    return diagnostics_;
  }

private:
  [[nodiscard]] static ResultCode lifecycle_result(const LifecycleState state) noexcept {
    switch (state) {
    case LifecycleState::Running:
      return ResultCode::AlreadyRunning;
    case LifecycleState::Stopping:
      return ResultCode::Stopping;
    case LifecycleState::Faulted:
      return ResultCode::Faulted;
    case LifecycleState::Stopped:
      return ResultCode::Ok;
    }
    return ResultCode::Faulted;
  }

  void update_after_start_failure(const ResultCode status) noexcept {
    set_lifecycle_diagnostic(LifecycleState::Stopped, status == ResultCode::Faulted
                                                          ? vehicle_core::TransportHealth::Faulted
                                                          : vehicle_core::TransportHealth::Stopped);
  }

  void set_lifecycle_diagnostic(const LifecycleState lifecycle,
                                const vehicle_core::TransportHealth transport) noexcept {
    std::lock_guard<std::mutex> lock{diagnostics_mutex_};
    diagnostics_.lifecycle = lifecycle;
    diagnostics_.transport = transport;
    diagnostics_.acquisition = source_->statistics();
  }

  [[nodiscard]] StatusResult stop_source_once() noexcept {
    if (!source_started_ || source_stop_called_)
      return source_stop_result_;
    source_stop_called_ = true;
    source_stop_result_ = source_->stop();
    // A source stop is an ownership handoff even when the source reports a
    // bounded timeout. A subsequent Runtime::stop() retries only the worker
    // join; it never invokes a non-idempotent source twice.
    source_started_ = false;
    return source_stop_result_;
  }

  void shutdown_noexcept() noexcept {
    stop_requested_.store(true, std::memory_order_release);
    if (source_started_)
      (void)stop_source_once();
#if !defined(ESP_PLATFORM)
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id())
      worker_.join();
#else
    // A FreeRTOS task cannot outlive this inline implementation. The bounded
    // public stop path reports a timeout to callers, while destruction waits
    // for the source's one-shot cancellation request to be acknowledged.
    constexpr TickType_t kWaitTicks = pdMS_TO_TICKS(1) == 0 ? 1 : pdMS_TO_TICKS(1);
    while (worker_active_.load(std::memory_order_acquire))
      vTaskDelay(kWaitTicks);
#endif
  }

  [[nodiscard]] TransportDiagnostics
  snapshot_and_update(const ReceiveStatus status,
                      const vehicle_core::MonotonicTimestamp receive_time_us,
                      const vehicle_core::RawCanFrame *frame = nullptr,
                      const ProcessResult *result = nullptr) noexcept {
    std::lock_guard<std::mutex> lock{diagnostics_mutex_};
    diagnostics_.acquisition = source_->statistics();
    if (status == ReceiveStatus::Frame && frame != nullptr) {
      diagnostics_.transport = vehicle_core::TransportHealth::Live;
      diagnostics_.has_last_frame = true;
      // This timestamp is sampled from the injected acquisition clock. It is
      // intentionally not RawCanFrame::timestamp_us, which may use a source
      // clock with a different epoch or be zero in host fakes.
      diagnostics_.last_frame_us = receive_time_us;
      if (result != nullptr) {
        switch (result->status) {
        case ProcessStatus::Processed:
          ++diagnostics_.frames_processed;
          break;
        case ProcessStatus::Ignored:
          ++diagnostics_.frames_ignored;
          break;
        case ProcessStatus::Malformed:
          ++diagnostics_.frames_malformed;
          break;
        case ProcessStatus::Fault:
          ++diagnostics_.processor_faults;
          diagnostics_.lifecycle = LifecycleState::Faulted;
          diagnostics_.transport = vehicle_core::TransportHealth::Faulted;
          break;
        }
      }
    } else if (status == ReceiveStatus::Timeout) {
      const auto now = clock_->now();
      const auto timeout_us = milliseconds_to_microseconds(config_.transport_silence_timeout_ms);
      if (diagnostics_.has_last_frame && now >= diagnostics_.last_frame_us &&
          now - diagnostics_.last_frame_us > timeout_us) {
        diagnostics_.transport = vehicle_core::TransportHealth::TimedOut;
      } else {
        diagnostics_.transport = vehicle_core::TransportHealth::AwaitingTraffic;
      }
    } else if (status == ReceiveStatus::Fault || status == ReceiveStatus::NotStarted) {
      diagnostics_.lifecycle = LifecycleState::Faulted;
      diagnostics_.transport = vehicle_core::TransportHealth::Faulted;
    }
    return diagnostics_;
  }

  void run_loop() noexcept {
    while (!stop_requested_.load(std::memory_order_acquire)) {
      vehicle_core::RawCanFrame frame{};
      const auto receive_status = source_->receive(frame, config_.receive_timeout_ms);
      const auto receive_time_us = clock_->now();
      if (stop_requested_.load(std::memory_order_acquire))
        break;

      if (receive_status == ReceiveStatus::Frame) {
        const auto result = processor_->process(frame);
        const auto snapshot = snapshot_and_update(receive_status, receive_time_us, &frame, &result);
        observer_->on_frame_processed(frame, result);
        observer_->on_diagnostics(snapshot);
        if (result.status == ProcessStatus::Fault) {
          lifecycle_.store(LifecycleState::Faulted, std::memory_order_release);
          stop_requested_.store(true, std::memory_order_release);
        }
        continue;
      }

      const auto snapshot = snapshot_and_update(receive_status, receive_time_us);
      observer_->on_diagnostics(snapshot);
      if (receive_status == ReceiveStatus::Fault || receive_status == ReceiveStatus::NotStarted) {
        lifecycle_.store(LifecycleState::Faulted, std::memory_order_release);
        stop_requested_.store(true, std::memory_order_release);
      }
    }
    worker_active_.store(false, std::memory_order_release);
  }

#if defined(ESP_PLATFORM)
  static void task_entry(void *raw) noexcept {
    auto *implementation = static_cast<RuntimeImplementation *>(raw);
    implementation->run_loop();
    implementation->task_ = nullptr;
    vTaskDelete(nullptr);
  }
#endif

  AcquisitionSource *source_;
  FrameProcessor *processor_;
  Observer *observer_;
  const vehicle_core::MonotonicClock *clock_;
  mutable std::mutex lifecycle_mutex_{};
  mutable std::mutex diagnostics_mutex_{};
  RuntimeConfig config_{};
  TransportDiagnostics diagnostics_{};
  std::atomic<LifecycleState> lifecycle_{LifecycleState::Stopped};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> worker_active_{false};
  bool source_started_{false};
  bool source_stop_called_{false};
  StatusResult source_stop_result_{ResultCode::Ok};
#if defined(ESP_PLATFORM)
  TaskHandle_t task_{nullptr};
#else
  std::thread worker_{};
#endif
};

static_assert(sizeof(RuntimeImplementation) <= Runtime::kImplementationStorageBytes,
              "vehicle telemetry runtime exceeds its fixed inline storage");
static_assert(alignof(RuntimeImplementation) <= alignof(Runtime),
              "vehicle telemetry runtime storage alignment is insufficient");

} // namespace

Runtime::Runtime(AcquisitionSource &source, FrameProcessor &processor, Observer &observer) noexcept
    : Runtime(source, processor, observer, system_clock()) {}

Runtime::Runtime(AcquisitionSource &source, FrameProcessor &processor, Observer &observer,
                 const vehicle_core::MonotonicClock &clock) noexcept {
  new (implementation_storage_) RuntimeImplementation{source, processor, observer, clock};
}

Runtime::~Runtime() noexcept {
  auto *implementation = reinterpret_cast<RuntimeImplementation *>(implementation_storage_);
  implementation->~RuntimeImplementation();
}

StatusResult Runtime::configure(const RuntimeConfig &config) noexcept {
  return reinterpret_cast<RuntimeImplementation *>(implementation_storage_)->configure(config);
}

StatusResult Runtime::start() noexcept {
  return reinterpret_cast<RuntimeImplementation *>(implementation_storage_)->start();
}

StatusResult Runtime::stop() noexcept {
  return reinterpret_cast<RuntimeImplementation *>(implementation_storage_)->stop();
}

LifecycleState Runtime::lifecycle() const noexcept {
  return reinterpret_cast<const RuntimeImplementation *>(implementation_storage_)->lifecycle();
}

TransportDiagnostics Runtime::diagnostics() const noexcept {
  return reinterpret_cast<const RuntimeImplementation *>(implementation_storage_)->diagnostics();
}

} // namespace vehicle_telemetry

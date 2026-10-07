#include <atomic>
#include <cstdio>
#include <new>

// Configure FreeRTOS before the hook header includes its port macros.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_freertos_hooks.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "report.hpp"
#include "sdkconfig.h"
#include "trace.hpp"

namespace {
class EspPlatform final : public runtime_baseline::Platform {
public:
  std::uint64_t now() const noexcept override {
    return static_cast<std::uint64_t>(esp_timer_get_time());
  }
  void busy(std::uint32_t us) noexcept override {
    if (us == 0)
      return;
    const auto until = now() + us;
    while (now() < until) {
    }
  }
  void wait(std::uint32_t us, const std::atomic<bool> &cancelled) noexcept override {
    if (!cancelled.load()) {
      const auto ticks =
          (static_cast<std::uint64_t>(us) * configTICK_RATE_HZ + 999'999) / 1'000'000;
      vTaskDelay(static_cast<TickType_t>(ticks ? ticks : 1));
    }
  }
  void wake() noexcept override {} // Receive waits are bounded to one tick.
  void after_float(std::array<std::uint64_t, 2> &running,
                   std::array<std::uint64_t, 2> &affinity) noexcept override {
    const auto cpu = xPortGetCoreID();
    const auto pinned = xTaskGetCoreID(nullptr);
    if (cpu < running.size())
      ++running[cpu];
    if (pinned < affinity.size())
      ++affinity[pinned];
  }
};

struct Idle final {
  std::uint32_t count{0};
  std::uint32_t maximum_gap_us{0};
  std::uint64_t last_us{0};
};
portMUX_TYPE idle_lock = portMUX_INITIALIZER_UNLOCKED;
Idle idle[2];
bool measuring{false};
bool idle_hook() {
  const auto cpu = xPortGetCoreID();
  portENTER_CRITICAL(&idle_lock);
  if (measuring && cpu < 2) {
    auto &sample = idle[cpu];
    const auto now = static_cast<std::uint64_t>(esp_timer_get_time());
    sample.maximum_gap_us =
        std::max(sample.maximum_gap_us, static_cast<std::uint32_t>(now - sample.last_us));
    sample.last_us = now;
    ++sample.count;
  }
  portEXIT_CRITICAL(&idle_lock);
  return true;
}
void begin_idle(std::uint64_t started) {
  portENTER_CRITICAL(&idle_lock);
  for (auto &sample : idle)
    sample = {0, 0, started};
  measuring = true;
  portEXIT_CRITICAL(&idle_lock);
}
void end_idle(runtime_baseline::Run &run) {
  portENTER_CRITICAL(&idle_lock);
  measuring = false;
  const auto stopped = static_cast<std::uint64_t>(esp_timer_get_time());
  for (unsigned cpu = 0; cpu < 2; ++cpu) {
    run.idle_calls[cpu] = idle[cpu].count;
    run.maximum_idle_gap_us[cpu] =
        std::max(idle[cpu].maximum_gap_us, static_cast<std::uint32_t>(stopped - idle[cpu].last_us));
  }
  portEXIT_CRITICAL(&idle_lock);
}
} // namespace

extern "C" void app_main() {
  // Main is a measurement controller, not part of the production scheduler.
  // Its higher priority ensures the finite load can be stopped on schedule.
  vTaskPrioritySet(nullptr, tskIDLE_PRIORITY + 3);
  if (esp_register_freertos_idle_hook_for_cpu(idle_hook, 0) != ESP_OK)
    return;
  if (esp_register_freertos_idle_hook_for_cpu(idle_hook, 1) != ESP_OK) {
    esp_deregister_freertos_idle_hook_for_cpu(idle_hook, 0);
    return;
  }
  std::printf("metadata,esp32,idf=%s,cpu_mhz=%u,tick_hz=%u,diagnostics=both_callbacks,"
              "receive_timeout_ms=1,duration_ms=%u,repeats=%u,worker_priority=1,"
              "controller_priority=3,idle_hooks=both,optimization_perf=%u,compiler=%s,"
              "checkpoint_wall_only=1,watchdog_timeout_s=%u,watchdog_panic=%u\n",
              esp_get_idf_version(), CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, configTICK_RATE_HZ,
              CONFIG_BASELINE_DURATION_MS, CONFIG_BASELINE_REPETITIONS,
#if CONFIG_COMPILER_OPTIMIZATION_PERF
              1U,
#else
              0U,
#endif
              __VERSION__, CONFIG_ESP_TASK_WDT_TIMEOUT_S,
#if CONFIG_ESP_TASK_WDT_PANIC
              1U
#else
              0U
#endif
  );
  runtime_baseline::print_header();
  static EspPlatform platform;
  // Runtime inline storage is larger than app_main's stack. Construct each
  // fixture/runtime once per run in static storage; their destructors join.
  alignas(runtime_baseline::Fixture) static std::byte
      fixture_storage[sizeof(runtime_baseline::Fixture)];
  alignas(vehicle_telemetry::Runtime) static std::byte
      runtime_storage[sizeof(vehicle_telemetry::Runtime)];
  for (unsigned index = 0; index < runtime_baseline::scenarios.size(); ++index) {
    if (CONFIG_BASELINE_SCENARIO != 0 && CONFIG_BASELINE_SCENARIO != index + 1)
      continue;
    auto scenario = runtime_baseline::scenarios[index];
    if (index != 4)
      scenario.processor_work_us = CONFIG_BASELINE_PROCESSOR_WORK_US;
    if (index == 2 || index == 3) {
      scenario.initial_frames = CONFIG_BASELINE_ARRIVAL_BATCH;
      scenario.arrival_batch = CONFIG_BASELINE_ARRIVAL_BATCH;
      scenario.arrival_interval_us = CONFIG_BASELINE_ARRIVAL_INTERVAL_US;
    }
    if (index == 3)
      scenario.observer_work_us = CONFIG_BASELINE_OBSERVER_WORK_US;
    for (bool timing : {false, true}) {
      for (unsigned repeat = 0; repeat < CONFIG_BASELINE_REPETITIONS; ++repeat) {
        auto *fixture = new (fixture_storage) runtime_baseline::Fixture{platform, scenario, timing};
        auto *runtime = new (runtime_storage)
            vehicle_telemetry::Runtime{fixture->source, fixture->processor, fixture->observer};
        vehicle_telemetry::RuntimeConfig config{};
        config.receive_timeout_ms = 1;
        const auto started = platform.now();
        begin_idle(started);
        baseline_trace_begin();
        const bool configured = runtime->configure(config).ok();
        const bool running = configured && runtime->start().ok();
        if (running)
          vTaskDelay(pdMS_TO_TICKS(CONFIG_BASELINE_DURATION_MS));
        const auto stopping = platform.now();
        const auto result = runtime->stop();
        const auto stopped = platform.now();
        runtime_baseline::Run run{stopped - started, stopped - stopping, {}, {}};
        end_idle(run);
        run.residency = baseline_trace_end();
        run.watchdog_events = baseline_watchdog_events();
        if (running && result.ok())
          runtime_baseline::print_run("esp32", scenario, repeat, *fixture, run);
        else
          std::printf("baseline_error,%s,%u\n", scenario.name,
                      static_cast<unsigned>(result.status));
        runtime->~Runtime();
        fixture->~Fixture();
        vTaskDelay(pdMS_TO_TICKS(100)); // Idle recovery outside each measured interval.
      }
    }
  }
  esp_deregister_freertos_idle_hook_for_cpu(idle_hook, 0);
  esp_deregister_freertos_idle_hook_for_cpu(idle_hook, 1);
}

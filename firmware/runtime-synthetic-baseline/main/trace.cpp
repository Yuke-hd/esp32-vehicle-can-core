#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_attr.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "trace.hpp"

namespace {
portMUX_TYPE trace_lock = portMUX_INITIALIZER_UNLOCKED;
runtime_baseline::TaskResidency residency;
std::atomic<bool> measuring{false};
std::atomic<std::uint32_t> watchdog_events{0};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
#if CONFIG_BASELINE_SCHEDULER_TRACE
// Both getters must remain IRAM-safe when called from the dispatcher wrapper.
#if CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH || CONFIG_FREERTOS_SMP || !CONFIG_IDF_TARGET_ESP32
#error "The synthetic scheduler wrapper requires ESP32 IDF FreeRTOS with getters in IRAM"
#endif
#if configMAX_TASK_NAME_LEN < 16
#error "The synthetic worker name requires at least 16 configured task-name bytes"
#endif
DRAM_ATTR const char worker_name[] = "vehicle_telemetry";
bool IRAM_ATTR is_worker(TaskHandle_t task) noexcept {
  if (task == nullptr)
    return false;
  const auto *name = pcTaskGetName(task);
  const auto maximum = configMAX_TASK_NAME_LEN - 1;
  unsigned i = 0;
  while (i < maximum && worker_name[i] != '\0') {
    if (name[i] != worker_name[i])
      return false;
    ++i;
  }
  return name[i] == '\0';
}
#endif
} // namespace

// ESP-IDF's documented optional watchdog notification. No reset/feed/change.
extern "C" void IRAM_ATTR esp_task_wdt_isr_user_handler() {
  if (measuring.load(std::memory_order_relaxed))
    watchdog_events.fetch_add(1, std::memory_order_relaxed);
}

void baseline_trace_begin() noexcept {
  portENTER_CRITICAL_SAFE(&trace_lock);
  residency = {};
#if CONFIG_BASELINE_SCHEDULER_TRACE
  residency.enabled = true;
#endif
  watchdog_events.store(0);
  measuring.store(true);
  portEXIT_CRITICAL_SAFE(&trace_lock);
}
runtime_baseline::TaskResidency baseline_trace_end() noexcept {
  portENTER_CRITICAL_SAFE(&trace_lock);
  measuring.store(false);
  residency.finish(static_cast<std::uint64_t>(esp_timer_get_time()));
  const auto snapshot = residency;
  portEXIT_CRITICAL_SAFE(&trace_lock);
  return snapshot;
}
std::uint32_t baseline_watchdog_events() noexcept { return watchdog_events.load(); }

#if CONFIG_BASELINE_SCHEDULER_TRACE
extern "C" void __real_vTaskSwitchContext();
extern "C" void IRAM_ATTR __wrap_vTaskSwitchContext() {
  const auto cpu = xPortGetCoreID();
  const auto old_task = xTaskGetCurrentTaskHandle();
  // No aggregate lock is held while FreeRTOS acquires its own kernel lock.
  __real_vTaskSwitchContext();
  const auto selected = static_cast<std::uint64_t>(esp_timer_get_time());
  const auto new_task = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL_SAFE(&trace_lock);
  if (measuring.load(std::memory_order_relaxed)) {
    const bool new_is_worker = residency.worker == 0 && is_worker(new_task);
    residency.select(cpu, reinterpret_cast<std::uintptr_t>(old_task),
                     reinterpret_cast<std::uintptr_t>(new_task), selected, new_is_worker);
  }
  portEXIT_CRITICAL_SAFE(&trace_lock);
}
#endif

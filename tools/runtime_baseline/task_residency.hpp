#pragma once

#include <array>
#include <cstdint>

namespace runtime_baseline {

// Selection timestamps bound scheduler task residency, including ISR time.
// They are not interrupt-free CPU time. External synchronization is required.
struct TaskResidency final {
  struct Core final {
    std::uint64_t started_us{0}, maximum_us{0};
    std::uint64_t segments{0}, reselections{0};
    bool active{false};
    void close(std::uint64_t now) noexcept {
      if (active) {
        const auto duration = now >= started_us ? now - started_us : 0;
        if (duration > maximum_us)
          maximum_us = duration;
        ++segments;
        active = false;
      }
    }
  };
  std::array<Core, 2> cores{};
  std::uintptr_t worker{0};
  bool enabled{false};

  void select(unsigned cpu, std::uintptr_t old_task, std::uintptr_t new_task,
              std::uint64_t selected_us, bool new_is_worker) noexcept {
    if (cpu >= cores.size())
      return;
    auto &core = cores[cpu];
    if (new_is_worker && worker == 0)
      worker = new_task;
    if (old_task == new_task) {
      if (new_task == worker && worker != 0)
        ++core.reselections;
      return; // A tick which selects the same task does not interrupt residency.
    }
    if (old_task == worker)
      core.close(selected_us);
    if (new_task == worker && worker != 0) {
      core.started_us = selected_us;
      core.active = true;
    }
  }
  void finish(std::uint64_t now) noexcept {
    for (auto &core : cores)
      core.close(now);
  }
};
} // namespace runtime_baseline

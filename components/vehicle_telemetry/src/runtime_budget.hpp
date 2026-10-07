#pragma once

#include "vehicle_telemetry/runtime.hpp"

namespace vehicle_telemetry::detail {

enum class BudgetAction { Continue, Checkpoint, Pause };

// Worker-owned counters. A checkpoint never clears total runnable work; only
// the caller completing a positive backend pause may invoke after_pause().
class WorkBudget final {
public:
  WorkBudget(const RuntimeConfig &config, vehicle_core::MonotonicTimestamp now) noexcept
      : config_(config), batch_started_(now), runnable_started_(now) {}

  [[nodiscard]] BudgetAction after_receive(bool frame,
                                           vehicle_core::MonotonicTimestamp now) noexcept {
    ++receive_calls_;
    if (frame)
      ++batch_frames_;
    // Test total limits first so coincident batch boundaries cannot defer them.
    if (receive_calls_ >= config_.max_runnable_receive_calls ||
        elapsed(now, runnable_started_, config_.max_runnable_time_us))
      return BudgetAction::Pause;
    if (!frame || batch_frames_ >= config_.max_frames_per_batch ||
        elapsed(now, batch_started_, config_.max_batch_time_us)) {
      batch_frames_ = 0;
      batch_started_ = now;
      return BudgetAction::Checkpoint;
    }
    return BudgetAction::Continue;
  }

  void after_pause(vehicle_core::MonotonicTimestamp now) noexcept {
    receive_calls_ = 0;
    batch_frames_ = 0;
    runnable_started_ = batch_started_ = now;
  }

private:
  [[nodiscard]] static bool elapsed(vehicle_core::MonotonicTimestamp now,
                                    vehicle_core::MonotonicTimestamp start,
                                    vehicle_core::Microseconds limit) noexcept {
    return now >= start && now - start >= limit;
  }
  const RuntimeConfig &config_;
  std::uint32_t receive_calls_{0};
  std::uint32_t batch_frames_{0};
  vehicle_core::MonotonicTimestamp batch_started_;
  vehicle_core::MonotonicTimestamp runnable_started_;
};
} // namespace vehicle_telemetry::detail

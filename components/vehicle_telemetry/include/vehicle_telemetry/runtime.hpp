#pragma once

#include <cstddef>
#include <cstdint>

#include "vehicle_core/time.hpp"
#include "vehicle_telemetry/lifecycle.hpp"
#include "vehicle_telemetry/observer.hpp"

namespace vehicle_telemetry {

struct RuntimeConfig final {
  // Acquisition sources use this bounded poll interval. Silence monitoring
  // uses an independent monotonic-clock value so sub-millisecond policies do
  // not get rounded up to the receive poll interval.
  std::uint32_t receive_timeout_ms{100};
  vehicle_core::Microseconds transport_silence_timeout_us{1'000'000};
  // Batch checkpoints refresh stop/silence checks; they do not block or reset
  // the accumulated runnable budget. Time limits include one full transaction
  // of possible overshoot: receive, processor and both observer callbacks.
  std::uint32_t max_frames_per_batch{16};
  vehicle_core::Microseconds max_batch_time_us{2'000};
  // Every receive return counts, including immediate non-frame returns. The
  // real system monotonic clock drives fairness, independently of liveness.
  std::uint32_t max_runnable_receive_calls{512};
  vehicle_core::Microseconds max_runnable_time_us{20'000};
  // Positive blocking opportunity; ESP rounds up to ticks and checks stop
  // between one-tick chunks. A tick may be 10 ms on a 100 Hz build.
  std::uint32_t budget_pause_ms{1};
};

class Runtime final {
public:
  // The inline implementation keeps ownership and teardown deterministic for
  // embedded callers: constructing a Runtime never allocates a second runtime
  // object on the heap. The worker backend may still allocate its OS task or
  // host thread according to the selected platform.
  static constexpr std::size_t kImplementationStorageBytes = 16'384;

  Runtime(AcquisitionSource &source, FrameProcessor &processor, Observer &observer) noexcept;
  Runtime(AcquisitionSource &source, FrameProcessor &processor, Observer &observer,
          const vehicle_core::MonotonicClock &clock) noexcept;
  ~Runtime() noexcept;

  Runtime(const Runtime &) = delete;
  Runtime &operator=(const Runtime &) = delete;
  Runtime(Runtime &&) = delete;
  Runtime &operator=(Runtime &&) = delete;

  [[nodiscard]] StatusResult configure(const RuntimeConfig &config) noexcept;
  [[nodiscard]] StatusResult start() noexcept;
  [[nodiscard]] StatusResult stop() noexcept;
  [[nodiscard]] LifecycleState lifecycle() const noexcept;
  [[nodiscard]] TransportDiagnostics diagnostics() const noexcept;

private:
  alignas(std::max_align_t) std::byte implementation_storage_[kImplementationStorageBytes]{};
};

} // namespace vehicle_telemetry

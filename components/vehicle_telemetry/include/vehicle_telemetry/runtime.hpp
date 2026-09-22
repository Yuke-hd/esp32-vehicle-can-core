#pragma once

#include <cstddef>
#include <cstdint>

#include "vehicle_core/time.hpp"
#include "vehicle_telemetry/lifecycle.hpp"
#include "vehicle_telemetry/observer.hpp"

namespace vehicle_telemetry {

struct RuntimeConfig final {
  std::uint32_t receive_timeout_ms{100};
  std::uint32_t transport_silence_timeout_ms{1'000};
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

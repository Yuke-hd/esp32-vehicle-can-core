#pragma once

#include <cstdint>

#include "vehicle_core/frame.hpp"
#include "vehicle_telemetry/lifecycle.hpp"

namespace vehicle_telemetry {

enum class ReceiveStatus : std::uint8_t { Frame, Timeout, Fault, NotStarted };

struct AcquisitionStatistics final {
  std::uint64_t frames_received{0};
  std::uint64_t frames_dropped{0};
  std::uint64_t queue_overflows{0};
  std::uint64_t driver_errors{0};
  std::uint64_t missed_frames{0};
  std::uint64_t controller_resets{0};
  std::uint64_t bus_off_events{0};
};

class AcquisitionSource {
public:
  virtual ~AcquisitionSource() = default;

  [[nodiscard]] virtual StatusResult start() noexcept = 0;
  // stop() is a one-shot ownership handoff for each successful start. It
  // must request cancellation of receive() before returning, including when
  // it reports Timeout or Stopping. Runtime never calls stop() twice for one
  // acquisition interval; a later Runtime::stop() only waits for the worker.
  [[nodiscard]] virtual StatusResult stop() noexcept = 0;
  [[nodiscard]] virtual ReceiveStatus receive(vehicle_core::RawCanFrame &frame,
                                              std::uint32_t timeout_ms) noexcept = 0;
  [[nodiscard]] virtual AcquisitionStatistics statistics() const noexcept = 0;
};

} // namespace vehicle_telemetry

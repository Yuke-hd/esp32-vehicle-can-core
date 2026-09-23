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
  // it reports Timeout or Stopping. Runtime does not repeat this call unless
  // the source opts into the explicit retry contract below. Failed partial
  // starts also get one bounded retry by default.
  [[nodiscard]] virtual StatusResult stop() noexcept = 0;

  // A source that retains cleanup ownership after a failed stop may opt into
  // retries. Runtime calls retry_stop() on later stop attempts until cleanup
  // succeeds or the source reports NotRunning. The default preserves the
  // one-shot contract for sources that cannot safely retry stop().
  [[nodiscard]] virtual bool supports_stop_retry() const noexcept { return false; }
  [[nodiscard]] virtual StatusResult retry_stop() noexcept { return {ResultCode::InvalidState}; }
  [[nodiscard]] virtual ReceiveStatus receive(vehicle_core::RawCanFrame &frame,
                                              std::uint32_t timeout_ms) noexcept = 0;
  [[nodiscard]] virtual AcquisitionStatistics statistics() const noexcept = 0;
};

} // namespace vehicle_telemetry

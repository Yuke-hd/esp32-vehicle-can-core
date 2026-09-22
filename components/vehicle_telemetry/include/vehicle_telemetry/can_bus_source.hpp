#pragma once

#include "can_bus/can_bus.h"
#include "vehicle_telemetry/receive.hpp"

namespace vehicle_telemetry {

// The core repository owns this optional ESP-IDF binding. Runtime remains
// source-injected, so controllers can replace it with a simulator, another
// CAN implementation, or a test double without changing the runtime.
class CanBusSource final : public AcquisitionSource {
public:
  explicit CanBusSource(can_bus::Configuration configuration) noexcept
      : configuration_(configuration) {}

  [[nodiscard]] StatusResult start() noexcept override;
  [[nodiscard]] StatusResult stop() noexcept override;
  [[nodiscard]] ReceiveStatus receive(vehicle_core::RawCanFrame &frame,
                                      std::uint32_t timeout_ms) noexcept override;
  [[nodiscard]] AcquisitionStatistics statistics() const noexcept override;

private:
  can_bus::Configuration configuration_{};
};

} // namespace vehicle_telemetry

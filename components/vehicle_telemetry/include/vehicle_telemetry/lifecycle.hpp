#pragma once

#include <cstdint>

namespace vehicle_telemetry {

enum class ResultCode : std::uint8_t {
  Ok,
  InvalidConfiguration,
  InvalidState,
  AlreadyRunning,
  NotRunning,
  Stopping,
  Timeout,
  Faulted,
  CapacityExceeded,
};

struct StatusResult final {
  ResultCode status{ResultCode::Ok};

  [[nodiscard]] constexpr bool ok() const noexcept { return status == ResultCode::Ok; }
};

enum class LifecycleState : std::uint8_t { Stopped, Running, Stopping, Faulted };

} // namespace vehicle_telemetry

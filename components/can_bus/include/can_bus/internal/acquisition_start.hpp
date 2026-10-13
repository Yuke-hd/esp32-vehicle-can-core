#pragma once

#include "can_bus/can_bus.h"

namespace can_bus::internal {

// Internal adapter result: ownership belongs to this startup attempt, not a
// later sample of the singleton lifecycle after another caller may start.
struct StartOutcome final {
  Result result;
  bool cleanup_required;
};

[[nodiscard]] StartOutcome start_with_ownership(const Configuration &configuration) noexcept;

} // namespace can_bus::internal

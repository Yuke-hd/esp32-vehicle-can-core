#pragma once

// Compatibility umbrella for the portable vehicle_core primitives. Make/model
// state, definitions, and decoders intentionally live in downstream
// controller repositories.
#include "vehicle_core/frame.hpp"
#include "vehicle_core/signal.hpp"
#include "vehicle_core/time.hpp"

namespace vehicle_core {

constexpr std::uint32_t kApiVersion = 1;

[[nodiscard]] bool library_is_available() noexcept;

} // namespace vehicle_core

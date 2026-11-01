#pragma once

#include <cstdint>
#include <optional>

namespace vehicle_core {

// Availability is deliberately independent of validation evidence. In
// particular, FreshnessUnverified is not a synonym for Fresh.
enum class Availability : std::uint8_t {
  NoData,
  Fresh,
  Stale,
  FreshnessUnverified,
  Unavailable,
};

enum class ValidationStatus : std::uint8_t { Reference, Observed, Confirmed };

template <typename T> struct Reading {
  std::optional<T> value{};
  Availability availability{Availability::NoData};
  ValidationStatus validation{ValidationStatus::Reference};
};

} // namespace vehicle_core

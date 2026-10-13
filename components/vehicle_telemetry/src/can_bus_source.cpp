#include "vehicle_telemetry/can_bus_source.hpp"

#include "can_bus/internal/acquisition_start.hpp"

namespace vehicle_telemetry {
namespace {

[[nodiscard]] StatusResult map_start_result(const can_bus::Result result) noexcept {
  switch (result) {
  case can_bus::Result::kOk:
    return {ResultCode::Ok};
  case can_bus::Result::kInvalidConfiguration:
    return {ResultCode::InvalidConfiguration};
  case can_bus::Result::kAlreadyStarted:
    return {ResultCode::AlreadyRunning};
  case can_bus::Result::kStopping:
    return {ResultCode::Stopping};
  case can_bus::Result::kFaulted:
  case can_bus::Result::kDriverFailure:
  case can_bus::Result::kTaskFailure:
    return {ResultCode::Faulted};
  case can_bus::Result::kNotStarted:
  case can_bus::Result::kTimeout:
    return {ResultCode::InvalidState};
  }
  return {ResultCode::Faulted};
}

[[nodiscard]] StatusResult map_stop_result(const can_bus::Result result) noexcept {
  switch (result) {
  case can_bus::Result::kOk:
    return {ResultCode::Ok};
  case can_bus::Result::kNotStarted:
    return {ResultCode::NotRunning};
  case can_bus::Result::kStopping:
    return {ResultCode::Stopping};
  case can_bus::Result::kTimeout:
    return {ResultCode::Timeout};
  case can_bus::Result::kFaulted:
  case can_bus::Result::kDriverFailure:
  case can_bus::Result::kTaskFailure:
    return {ResultCode::Faulted};
  case can_bus::Result::kInvalidConfiguration:
  case can_bus::Result::kAlreadyStarted:
    return {ResultCode::InvalidState};
  }
  return {ResultCode::Faulted};
}

} // namespace

StatusResult CanBusSource::start() noexcept {
  const auto outcome = can_bus::internal::start_with_ownership(configuration_);
  if (outcome.cleanup_required)
    owns_acquisition_ = true;
  return map_start_result(outcome.result);
}

StatusResult CanBusSource::stop() noexcept {
  if (!owns_acquisition_)
    return {ResultCode::NotRunning};
  const auto result = map_stop_result(can_bus::stop());
  if (result.ok() || result.status == ResultCode::NotRunning)
    owns_acquisition_ = false;
  return result;
}

bool CanBusSource::supports_stop_retry() const noexcept { return true; }

StatusResult CanBusSource::retry_stop() noexcept { return stop(); }

ReceiveStatus CanBusSource::receive(vehicle_core::RawCanFrame &frame,
                                    const std::uint32_t timeout_ms) noexcept {
  switch (can_bus::receive(frame, timeout_ms)) {
  case can_bus::Result::kOk:
    return ReceiveStatus::Frame;
  case can_bus::Result::kTimeout:
    return ReceiveStatus::Timeout;
  case can_bus::Result::kNotStarted:
    return ReceiveStatus::NotStarted;
  case can_bus::Result::kStopping:
  case can_bus::Result::kFaulted:
  case can_bus::Result::kDriverFailure:
  case can_bus::Result::kTaskFailure:
  case can_bus::Result::kInvalidConfiguration:
  case can_bus::Result::kAlreadyStarted:
    return ReceiveStatus::Fault;
  }
  return ReceiveStatus::Fault;
}

AcquisitionStatistics CanBusSource::statistics() const noexcept {
  const auto stats = can_bus::statistics();
  return AcquisitionStatistics{
      stats.frames_received,  stats.frames_dropped,    stats.queue_overflows, stats.bus_errors,
      stats.driver_rx_missed, stats.controller_resets, stats.bus_off_events};
}

} // namespace vehicle_telemetry

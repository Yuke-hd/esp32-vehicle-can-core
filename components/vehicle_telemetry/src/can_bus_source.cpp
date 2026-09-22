#include "vehicle_telemetry/can_bus_source.hpp"

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
  return map_start_result(can_bus::start(configuration_));
}

StatusResult CanBusSource::stop() noexcept { return map_stop_result(can_bus::stop()); }

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

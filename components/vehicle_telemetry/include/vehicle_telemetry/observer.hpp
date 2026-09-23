#pragma once

#include <cstdint>

#include "vehicle_core/frame.hpp"
#include "vehicle_core/health.hpp"
#include "vehicle_telemetry/lifecycle.hpp"
#include "vehicle_telemetry/receive.hpp"

namespace vehicle_telemetry {

enum class ProcessStatus : std::uint8_t { Ignored, Processed, Malformed, Fault };

struct ProcessResult final {
  ProcessStatus status{ProcessStatus::Ignored};
};

struct TransportDiagnostics final {
  LifecycleState lifecycle{LifecycleState::Stopped};
  vehicle_core::TransportHealth transport{vehicle_core::TransportHealth::Stopped};
  AcquisitionStatistics acquisition{};
  std::uint64_t frames_processed{0};
  std::uint64_t frames_ignored{0};
  std::uint64_t frames_malformed{0};
  std::uint64_t processor_faults{0};
  bool has_last_frame{false};
  vehicle_core::MonotonicTimestamp last_frame_us{0};
};

class FrameProcessor {
public:
  virtual ~FrameProcessor() = default;

  virtual void reset() noexcept = 0;
  [[nodiscard]] virtual ProcessResult process(const vehicle_core::RawCanFrame &frame) noexcept = 0;
};

class Observer {
public:
  virtual ~Observer() = default;

  virtual void on_frame_processed(const vehicle_core::RawCanFrame &frame,
                                  const ProcessResult &result) noexcept = 0;
  virtual void on_diagnostics(const TransportDiagnostics &diagnostics) noexcept = 0;
};

} // namespace vehicle_telemetry

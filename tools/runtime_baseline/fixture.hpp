#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

#include "vehicle_telemetry/runtime.hpp"

// Synthetic-only test support. No CAN driver, hardware pins, or protocol data.
namespace runtime_baseline {

class Platform {
public:
  virtual ~Platform() = default;
  virtual std::uint64_t now() const noexcept = 0;
  virtual void busy(std::uint32_t us) noexcept = 0;
  virtual void wait(std::uint32_t us, const std::atomic<bool> &cancelled) noexcept = 0;
  virtual void wake() noexcept = 0;
  virtual void after_float(std::array<std::uint64_t, 2> &running,
                           std::array<std::uint64_t, 2> &affinity) noexcept {
    (void)running;
    (void)affinity;
  }
};

struct Scenario {
  const char *name;
  std::uint32_t initial_frames;
  std::uint32_t arrival_batch;
  std::uint32_t arrival_interval_us; // Zero replenishes only when empty.
  std::uint32_t processor_work_us;
  std::uint32_t observer_work_us; // Applied separately to BOTH callback kinds.
  std::uint32_t frame_limit;      // Parks at a source wait until cancelled.
  bool mixed;
};

inline constexpr std::array<Scenario, 5> scenarios{{
    {"ready", 1, 1, 0, 10, 0, 0xFFFF'FFFFU, false},
    {"mixed", 1, 1, 0, 10, 0, 0xFFFF'FFFFU, true},
    {"burst", 64, 64, 1000, 10, 0, 0xFFFF'FFFFU, true},
    {"slow_observer", 64, 64, 1000, 10, 200, 0xFFFF'FFFFU, false},
    {"cancel_wait", 0, 0, 0, 0, 0, 0xFFFF'FFFFU, false},
}};

struct Cost {
  std::uint64_t count{0};
  std::uint64_t total_us{0};
  std::uint64_t maximum_us{0};
  void add(std::uint64_t us) noexcept {
    ++count;
    total_us += us;
    maximum_us = std::max(maximum_us, us);
  }
};

// Worker-owned aggregates; read only after Runtime::stop() has joined.
struct Metrics {
  Cost receive_cost, source_wait_cost, processor_cost;
  Cost frame_observer_cost, diagnostic_observer_cost;
  std::uint64_t processed{0}, ignored{0}, malformed{0};
  std::uint64_t source_waits{0};
  std::uint64_t maximum_frames_between_waits{0};
  std::uint64_t maximum_checkpoint_span_us{0};
  std::array<std::uint64_t, 2> running_core_after_float{};
  std::array<std::uint64_t, 2> affinity_after_float{};
  vehicle_telemetry::TransportDiagnostics last_diagnostics{};
  bool timing{true};
  bool segment_active{false};
  std::uint64_t segment_started_us{0}, segment_frames{0};

  std::uint64_t frames() const noexcept { return processed + ignored + malformed; }
  void begin_segment(std::uint64_t now) noexcept {
    if (!segment_active) {
      segment_active = true;
      segment_started_us = now;
    }
  }
  void sample_segment(std::uint64_t now) noexcept {
    maximum_frames_between_waits = std::max(maximum_frames_between_waits, segment_frames);
    if (timing && segment_active)
      maximum_checkpoint_span_us = std::max(maximum_checkpoint_span_us, now - segment_started_us);
  }
  void checkpoint(std::uint64_t now) noexcept {
    sample_segment(now);
    segment_active = false;
    segment_frames = 0;
    ++source_waits;
  }
};

class Queue final {
public:
  static constexpr std::uint32_t capacity = 128;
  void add(std::uint64_t count) noexcept {
    const auto accepted = std::min<std::uint64_t>(count, capacity - size_);
    for (std::uint32_t i = 0; i < accepted; ++i)
      entries_[(head_ + size_ + i) % capacity] = generated + i;
    generated += count;
    dropped += count - accepted;
    size_ += static_cast<std::uint32_t>(accepted);
    maximum_backlog = std::max(maximum_backlog, size_);
  }
  bool pop(std::uint64_t &sequence) noexcept {
    if (size_ == 0)
      return false;
    sequence = entries_[head_];
    head_ = (head_ + 1) % capacity;
    --size_;
    ++consumed;
    return true;
  }
  std::uint32_t size() const noexcept { return size_; }
  std::uint64_t generated{0}, consumed{0}, dropped{0};
  std::uint32_t maximum_backlog{0};

private:
  std::array<std::uint64_t, capacity> entries_{};
  std::uint32_t head_{0}, size_{0};
};

class Source final : public vehicle_telemetry::AcquisitionSource {
public:
  Source(Platform &platform, const Scenario &config, Metrics &metrics) noexcept
      : platform_(platform), config_(config), metrics_(metrics) {}
  vehicle_telemetry::StatusResult start() noexcept override {
    cancelled_.store(false);
    queue_ = Queue{};
    queue_.add(config_.initial_frames);
    arrivals_at_us_ = platform_.now();
    publish_statistics();
    return {};
  }
  vehicle_telemetry::StatusResult stop() noexcept override {
    cancelled_.store(true);
    platform_.wake();
    return {};
  }
  vehicle_telemetry::ReceiveStatus receive(vehicle_core::RawCanFrame &frame,
                                           std::uint32_t timeout_ms) noexcept override {
    const auto started = platform_.now();
    metrics_.begin_segment(started);
    const auto status = receive_impl(frame, timeout_ms);
    if (metrics_.timing)
      metrics_.receive_cost.add(platform_.now() - started);
    return status;
  }
  vehicle_telemetry::AcquisitionStatistics statistics() const noexcept override {
    vehicle_telemetry::AcquisitionStatistics statistics{};
    statistics.frames_received = generated_.load();
    statistics.frames_dropped = dropped_.load();
    statistics.queue_overflows = dropped_.load();
    return statistics;
  }
  const Queue &queue() const noexcept { return queue_; } // After join only.

private:
  vehicle_telemetry::ReceiveStatus receive_impl(vehicle_core::RawCanFrame &frame,
                                                std::uint32_t timeout_ms) noexcept {
    if (cancelled_.load())
      return vehicle_telemetry::ReceiveStatus::NotStarted;
    const auto now = platform_.now();
    const bool at_limit = queue_.consumed >= config_.frame_limit;
    if (!at_limit) {
      if (config_.arrival_interval_us == 0) {
        if (queue_.size() == 0)
          queue_.add(config_.arrival_batch);
      } else {
        const auto batches = (now - arrivals_at_us_) / config_.arrival_interval_us;
        queue_.add(batches * config_.arrival_batch);
        arrivals_at_us_ += batches * config_.arrival_interval_us;
      }
      publish_statistics();
      std::uint64_t sequence{0};
      if (queue_.pop(sequence)) {
        frame = {};
        frame.identifier = static_cast<std::uint32_t>(sequence % 3);
        frame.timestamp_us = now;
        frame.dlc = config_.mixed && frame.identifier == 2 ? 9 : 1;
        return vehicle_telemetry::ReceiveStatus::Frame;
      }
    }
    // This is an explicit source WAIT REQUEST, not proof of an OS context
    // switch or real uninterrupted execution. A platform may resume early.
    const auto before_wait = platform_.now();
    metrics_.checkpoint(before_wait);
    const auto wait_us = std::min<std::uint32_t>(timeout_ms * 1000, 1000);
    platform_.wait(wait_us, cancelled_);
    const auto after_wait = platform_.now();
    if (metrics_.timing)
      metrics_.source_wait_cost.add(after_wait - before_wait);
    metrics_.begin_segment(after_wait);
    return cancelled_.load() ? vehicle_telemetry::ReceiveStatus::NotStarted
                             : vehicle_telemetry::ReceiveStatus::Timeout;
  }
  void publish_statistics() noexcept {
    generated_.store(queue_.generated);
    dropped_.store(queue_.dropped);
  }
  Platform &platform_;
  const Scenario &config_;
  Metrics &metrics_;
  Queue queue_{};
  std::atomic<bool> cancelled_{false};
  std::atomic<std::uint64_t> generated_{0}, dropped_{0};
  std::uint64_t arrivals_at_us_{0};
};

class Processor final : public vehicle_telemetry::FrameProcessor {
public:
  Processor(Platform &platform, const Scenario &config, Metrics &metrics) noexcept
      : platform_(platform), config_(config), metrics_(metrics) {}
  void reset() noexcept override { checksum_ = 0.5F; }
  vehicle_telemetry::ProcessResult
  process(const vehicle_core::RawCanFrame &frame) noexcept override {
    const auto before = metrics_.timing ? platform_.now() : 0;
    const auto status = !config_.mixed || frame.identifier == 0
                            ? vehicle_telemetry::ProcessStatus::Processed
                        : frame.identifier == 1 ? vehicle_telemetry::ProcessStatus::Ignored
                                                : vehicle_telemetry::ProcessStatus::Malformed;
    if (status == vehicle_telemetry::ProcessStatus::Processed) {
      // Volatile result prevents folding away representative single-precision
      // work. This is generic synthetic arithmetic, not a signal decoder.
      for (unsigned i = 0; i < 16; ++i)
        checksum_ = checksum_ * 0.99F + static_cast<float>(frame.identifier + i) * 0.001F;
      platform_.after_float(metrics_.running_core_after_float, metrics_.affinity_after_float);
      ++metrics_.processed;
    } else if (status == vehicle_telemetry::ProcessStatus::Ignored) {
      ++metrics_.ignored;
    } else {
      ++metrics_.malformed;
    }
    platform_.busy(config_.processor_work_us);
    ++metrics_.segment_frames;
    if (metrics_.timing)
      metrics_.processor_cost.add(platform_.now() - before);
    return {status};
  }

private:
  Platform &platform_;
  const Scenario &config_;
  Metrics &metrics_;
  volatile float checksum_{0.5F};
};

class Observer final : public vehicle_telemetry::Observer {
public:
  Observer(Platform &platform, const Scenario &config, Metrics &metrics) noexcept
      : platform_(platform), config_(config), metrics_(metrics) {}
  void on_frame_processed(const vehicle_core::RawCanFrame &,
                          const vehicle_telemetry::ProcessResult &) noexcept override {
    cost(metrics_.frame_observer_cost);
  }
  void
  on_diagnostics(const vehicle_telemetry::TransportDiagnostics &diagnostics) noexcept override {
    const auto before = metrics_.timing ? platform_.now() : 0;
    metrics_.last_diagnostics = diagnostics;
    platform_.busy(config_.observer_work_us);
    if (metrics_.timing)
      metrics_.diagnostic_observer_cost.add(platform_.now() - before);
    metrics_.sample_segment(metrics_.timing ? platform_.now() : 0);
  }

private:
  void cost(Cost &cost) noexcept {
    const auto before = metrics_.timing ? platform_.now() : 0;
    platform_.busy(config_.observer_work_us);
    if (metrics_.timing)
      cost.add(platform_.now() - before);
  }
  Platform &platform_;
  const Scenario &config_;
  Metrics &metrics_;
};

struct Fixture final {
  Fixture(Platform &platform, const Scenario &config, bool timing = true) noexcept
      : source(platform, config, metrics), processor(platform, config, metrics),
        observer(platform, config, metrics) {
    metrics.timing = timing;
  }
  Metrics metrics{};
  Source source;
  Processor processor;
  Observer observer;
};

} // namespace runtime_baseline

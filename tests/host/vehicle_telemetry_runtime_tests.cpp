#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "vehicle_telemetry/runtime.hpp"

namespace {

class ManualClock final : public vehicle_core::MonotonicClock {
public:
  [[nodiscard]] vehicle_core::MonotonicTimestamp now() const noexcept override {
    return now_us_.load(std::memory_order_acquire);
  }

  void set(const vehicle_core::MonotonicTimestamp now_us) noexcept {
    now_us_.store(now_us, std::memory_order_release);
  }

private:
  std::atomic<vehicle_core::MonotonicTimestamp> now_us_{0};
};

class FakeSource final : public vehicle_telemetry::AcquisitionSource {
public:
  explicit FakeSource(const std::size_t capacity = 4) : capacity_(capacity) {}

  [[nodiscard]] vehicle_telemetry::StatusResult start() noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    if (started_)
      return {vehicle_telemetry::ResultCode::AlreadyRunning};
    if (start_result_ != vehicle_telemetry::ResultCode::Ok) {
      if (start_failure_owns_source_) {
        started_ = true;
        stop_requested_ = false;
      }
      return {start_result_};
    }
    started_ = true;
    stop_requested_ = false;
    faulted_ = false;
    return {vehicle_telemetry::ResultCode::Ok};
  }

  [[nodiscard]] vehicle_telemetry::StatusResult stop() noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    ++stop_calls_;
    if (!started_)
      return {vehicle_telemetry::ResultCode::NotRunning};
    stop_requested_ = true;
    condition_.notify_all();
    if (stop_result_ == vehicle_telemetry::ResultCode::Ok)
      started_ = false;
    return {stop_result_};
  }

  [[nodiscard]] vehicle_telemetry::ReceiveStatus
  receive(vehicle_core::RawCanFrame &frame, const std::uint32_t timeout_ms) noexcept override {
    std::unique_lock<std::mutex> lock{mutex_};
    const auto ready = [this] { return !frames_.empty() || stop_requested_ || faulted_; };
    if (!condition_.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready))
      return vehicle_telemetry::ReceiveStatus::Timeout;
    if (faulted_)
      return vehicle_telemetry::ReceiveStatus::Fault;
    if (stop_requested_ || !started_)
      return vehicle_telemetry::ReceiveStatus::NotStarted;
    frame = frames_.front();
    frames_.pop_front();
    return vehicle_telemetry::ReceiveStatus::Frame;
  }

  [[nodiscard]] vehicle_telemetry::AcquisitionStatistics statistics() const noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    return statistics_;
  }

  bool inject(const vehicle_core::RawCanFrame &frame) noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    ++statistics_.frames_received;
    if (!started_ || frames_.size() >= capacity_) {
      ++statistics_.frames_dropped;
      ++statistics_.queue_overflows;
      return false;
    }
    frames_.push_back(frame);
    condition_.notify_one();
    return true;
  }

  void fail() noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    faulted_ = true;
    condition_.notify_all();
  }

  [[nodiscard]] std::uint32_t stop_calls() const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    return stop_calls_;
  }

  void set_stop_result(const vehicle_telemetry::ResultCode result) noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    stop_result_ = result;
  }

  void set_start_result(const vehicle_telemetry::ResultCode result,
                        const bool owns_source) noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    start_result_ = result;
    start_failure_owns_source_ = owns_source;
  }

private:
  const std::size_t capacity_;
  mutable std::mutex mutex_{};
  std::condition_variable condition_{};
  std::deque<vehicle_core::RawCanFrame> frames_{};
  vehicle_telemetry::AcquisitionStatistics statistics_{};
  std::uint32_t stop_calls_{0};
  vehicle_telemetry::ResultCode start_result_{vehicle_telemetry::ResultCode::Ok};
  vehicle_telemetry::ResultCode stop_result_{vehicle_telemetry::ResultCode::Ok};
  bool start_failure_owns_source_{false};
  bool started_{false};
  bool stop_requested_{false};
  bool faulted_{false};
};

class CountingProcessor final : public vehicle_telemetry::FrameProcessor {
public:
  void reset() noexcept override { ++reset_count; }

  [[nodiscard]] vehicle_telemetry::ProcessResult
  process(const vehicle_core::RawCanFrame &) noexcept override {
    ++process_count;
    return {result};
  }

  std::atomic<std::uint32_t> reset_count{0};
  std::atomic<std::uint32_t> process_count{0};
  vehicle_telemetry::ProcessStatus result{vehicle_telemetry::ProcessStatus::Processed};
};

class BlockingProcessor final : public vehicle_telemetry::FrameProcessor {
public:
  void reset() noexcept override {}

  [[nodiscard]] vehicle_telemetry::ProcessResult
  process(const vehicle_core::RawCanFrame &) noexcept override {
    entered.store(true, std::memory_order_release);
    while (blocked.load(std::memory_order_acquire))
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return {vehicle_telemetry::ProcessStatus::Processed};
  }

  std::atomic<bool> entered{false};
  std::atomic<bool> blocked{true};
};

class RecordingObserver final : public vehicle_telemetry::Observer {
public:
  void on_frame_processed(const vehicle_core::RawCanFrame &,
                          const vehicle_telemetry::ProcessResult &) noexcept override {
    frames_processed.fetch_add(1, std::memory_order_release);
    condition.notify_all();
  }

  void
  on_diagnostics(const vehicle_telemetry::TransportDiagnostics &diagnostics) noexcept override {
    {
      std::lock_guard<std::mutex> lock{mutex};
      last = diagnostics;
    }
    condition.notify_all();
  }

  bool wait_for_frame(const std::uint32_t expected) {
    std::unique_lock<std::mutex> lock{mutex};
    return condition.wait_for(lock, std::chrono::seconds(2), [this, expected] {
      return frames_processed.load(std::memory_order_acquire) >= expected;
    });
  }

  bool wait_for_transport(const vehicle_core::TransportHealth expected) {
    std::unique_lock<std::mutex> lock{mutex};
    return condition.wait_for(lock, std::chrono::seconds(2),
                              [this, expected] { return last.transport == expected; });
  }

  bool wait_for_lifecycle(const vehicle_telemetry::LifecycleState expected) {
    std::unique_lock<std::mutex> lock{mutex};
    return condition.wait_for(lock, std::chrono::seconds(2),
                              [this, expected] { return last.lifecycle == expected; });
  }

  [[nodiscard]] vehicle_telemetry::TransportDiagnostics diagnostics() const {
    std::lock_guard<std::mutex> lock{mutex};
    return last;
  }

  std::atomic<std::uint32_t> frames_processed{0};

private:
  mutable std::mutex mutex{};
  std::condition_variable condition{};
  vehicle_telemetry::TransportDiagnostics last{};
};

[[nodiscard]] vehicle_core::RawCanFrame frame(const std::uint32_t identifier) {
  vehicle_core::RawCanFrame result{};
  result.identifier = identifier;
  result.dlc = 1;
  result.data[0] = 0x42;
  return result;
}

} // namespace

TEST_CASE("generic runtime accepts an injected fake processor and source") {
  FakeSource source;
  CountingProcessor processor;
  RecordingObserver observer;
  ManualClock clock;
  clock.set(42);
  vehicle_telemetry::Runtime runtime{source, processor, observer, clock};

  CHECK(runtime.configure({10, 100'000}).ok());
  REQUIRE(runtime.start().ok());
  REQUIRE(source.inject(frame(0x123)));
  REQUIRE(observer.wait_for_frame(1));

  CHECK(processor.reset_count == 1);
  CHECK(processor.process_count == 1);
  CHECK(observer.diagnostics().frames_processed == 1);
  CHECK(observer.diagnostics().last_frame_us == 42);
  CHECK(runtime.stop().ok());
  CHECK(source.stop_calls() == 1);
}

TEST_CASE("generic runtime serializes lifecycle and source ownership") {
  FakeSource source;
  CountingProcessor processor;
  RecordingObserver observer;
  vehicle_telemetry::Runtime runtime{source, processor, observer};

  CHECK(runtime.configure({0, 100'000}).status ==
        vehicle_telemetry::ResultCode::InvalidConfiguration);
  CHECK(runtime.configure({10, 0}).status == vehicle_telemetry::ResultCode::InvalidConfiguration);
  CHECK(runtime.configure({10, 100'000}).ok());
  REQUIRE(runtime.start().ok());
  CHECK(runtime.start().status == vehicle_telemetry::ResultCode::AlreadyRunning);
  CHECK(runtime.configure({10, 100'000}).status == vehicle_telemetry::ResultCode::InvalidState);
  CHECK(runtime.stop().ok());
  CHECK(runtime.stop().status == vehicle_telemetry::ResultCode::NotRunning);
  CHECK(processor.reset_count == 1);
  CHECK(source.stop_calls() == 1);
}

TEST_CASE("generic runtime keeps non-owning start failures restartable") {
  FakeSource source;
  source.set_start_result(vehicle_telemetry::ResultCode::InvalidConfiguration, false);
  CountingProcessor processor;
  RecordingObserver observer;
  vehicle_telemetry::Runtime runtime{source, processor, observer};

  REQUIRE(runtime.configure({10, 100'000}).ok());
  CHECK(runtime.start().status == vehicle_telemetry::ResultCode::InvalidConfiguration);
  CHECK(runtime.lifecycle() == vehicle_telemetry::LifecycleState::Stopped);
  CHECK(source.stop_calls() == 0);

  source.set_start_result(vehicle_telemetry::ResultCode::Ok, false);
  REQUIRE(runtime.start().ok());
  CHECK(runtime.stop().ok());
  CHECK(source.stop_calls() == 1);
}

TEST_CASE("generic runtime never stops a source it did not start") {
  FakeSource source;
  REQUIRE(source.start().ok());
  CountingProcessor processor;
  RecordingObserver observer;
  vehicle_telemetry::Runtime runtime{source, processor, observer};

  REQUIRE(runtime.configure({10, 100'000}).ok());
  CHECK(runtime.start().status == vehicle_telemetry::ResultCode::AlreadyRunning);
  CHECK(runtime.lifecycle() == vehicle_telemetry::LifecycleState::Stopped);
  CHECK(source.stop_calls() == 0);
  CHECK(source.stop().ok());
}

TEST_CASE("generic runtime reconciles a failed start with no source ownership") {
  FakeSource source;
  source.set_start_result(vehicle_telemetry::ResultCode::Faulted, false);
  CountingProcessor processor;
  RecordingObserver observer;
  vehicle_telemetry::Runtime runtime{source, processor, observer};

  REQUIRE(runtime.configure({10, 100'000}).ok());
  CHECK(runtime.start().status == vehicle_telemetry::ResultCode::Faulted);
  CHECK(runtime.lifecycle() == vehicle_telemetry::LifecycleState::Stopped);
  CHECK(source.stop_calls() == 1);

  source.set_start_result(vehicle_telemetry::ResultCode::Ok, false);
  REQUIRE(runtime.start().ok());
  CHECK(runtime.stop().ok());
  CHECK(source.stop_calls() == 2);
}

TEST_CASE("generic runtime cleans partial starts and remains restartable") {
  FakeSource source;
  source.set_start_result(vehicle_telemetry::ResultCode::Faulted, true);
  CountingProcessor processor;
  RecordingObserver observer;
  vehicle_telemetry::Runtime runtime{source, processor, observer};

  REQUIRE(runtime.configure({10, 100'000}).ok());
  CHECK(runtime.start().status == vehicle_telemetry::ResultCode::Faulted);
  CHECK(runtime.lifecycle() == vehicle_telemetry::LifecycleState::Stopped);
  CHECK(source.stop_calls() == 1);

  source.set_start_result(vehicle_telemetry::ResultCode::Ok, false);
  REQUIRE(runtime.start().ok());
  CHECK(runtime.stop().ok());
  CHECK(source.stop_calls() == 2);
}

TEST_CASE("generic runtime retries failed partial-start cleanup once") {
  FakeSource source;
  source.set_start_result(vehicle_telemetry::ResultCode::Faulted, true);
  source.set_stop_result(vehicle_telemetry::ResultCode::Faulted);
  CountingProcessor processor;
  RecordingObserver observer;
  vehicle_telemetry::Runtime runtime{source, processor, observer};

  REQUIRE(runtime.configure({10, 100'000}).ok());
  CHECK(runtime.start().status == vehicle_telemetry::ResultCode::Faulted);
  CHECK(runtime.lifecycle() == vehicle_telemetry::LifecycleState::Faulted);
  CHECK(source.stop_calls() == 1);

  source.set_stop_result(vehicle_telemetry::ResultCode::Ok);
  CHECK(runtime.stop().ok());
  CHECK(runtime.lifecycle() == vehicle_telemetry::LifecycleState::Stopped);
  CHECK(source.stop_calls() == 2);

  source.set_start_result(vehicle_telemetry::ResultCode::Ok, false);
  REQUIRE(runtime.start().ok());
  CHECK(runtime.stop().ok());
  CHECK(source.stop_calls() == 3);
}

TEST_CASE("generic runtime reports injected-clock transport silence") {
  FakeSource source;
  CountingProcessor processor;
  RecordingObserver observer;
  ManualClock clock;
  vehicle_telemetry::Runtime runtime{source, processor, observer, clock};

  REQUIRE(runtime.configure({1, 100}).ok());
  REQUIRE(runtime.start().ok());
  clock.set(1'000);
  REQUIRE(source.inject(frame(0x321)));
  REQUIRE(observer.wait_for_frame(1));
  clock.set(1'099);
  CHECK(runtime.diagnostics().transport == vehicle_core::TransportHealth::Live);
  clock.set(1'100);
  CHECK(runtime.diagnostics().transport == vehicle_core::TransportHealth::TimedOut);
  CHECK(runtime.stop().ok());
}

TEST_CASE("generic runtime latches source faults and makes bounded progress") {
  FakeSource source;
  CountingProcessor processor;
  RecordingObserver observer;
  vehicle_telemetry::Runtime runtime{source, processor, observer};

  REQUIRE(runtime.configure({2, 100'000}).ok());
  REQUIRE(runtime.start().ok());
  source.fail();
  REQUIRE(observer.wait_for_lifecycle(vehicle_telemetry::LifecycleState::Faulted));
  CHECK(runtime.stop().ok());

  FakeSource bounded_source{2};
  BlockingProcessor blocking_processor;
  RecordingObserver bounded_observer;
  vehicle_telemetry::Runtime bounded_runtime{bounded_source, blocking_processor, bounded_observer};
  REQUIRE(bounded_runtime.configure({2, 100'000}).ok());
  REQUIRE(bounded_runtime.start().ok());
  REQUIRE(bounded_source.inject(frame(1)));
  for (int attempt = 0;
       attempt < 2'000 && !blocking_processor.entered.load(std::memory_order_acquire); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(blocking_processor.entered.load(std::memory_order_acquire));
  CHECK(bounded_source.inject(frame(2)));
  CHECK(bounded_source.inject(frame(3)));
  CHECK_FALSE(bounded_source.inject(frame(4)));
  CHECK(bounded_source.statistics().queue_overflows == 1);
  blocking_processor.blocked.store(false, std::memory_order_release);
  CHECK(bounded_runtime.stop().ok());
}

TEST_CASE("generic runtime stops a non-idempotent source once during timeout and destruction") {
  FakeSource source;
  source.set_stop_result(vehicle_telemetry::ResultCode::Timeout);
  CountingProcessor processor;
  RecordingObserver observer;
  {
    vehicle_telemetry::Runtime runtime{source, processor, observer};
    REQUIRE(runtime.configure({2, 100'000}).ok());
    REQUIRE(runtime.start().ok());
    CHECK(runtime.stop().status == vehicle_telemetry::ResultCode::Timeout);
  }
  CHECK(source.stop_calls() == 1);
}

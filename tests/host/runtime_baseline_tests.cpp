#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "fixture.hpp"
#include "task_residency.hpp"

namespace {
class ManualPlatform : public runtime_baseline::Platform {
public:
  std::uint64_t now() const noexcept override { return time.load(); }
  void busy(std::uint32_t us) noexcept override { time.fetch_add(us); }
  void wait(std::uint32_t us, const std::atomic<bool> &cancelled) noexcept override {
    time.fetch_add(us);
    if (gate) {
      std::unique_lock<std::mutex> lock{mutex};
      entered = true;
      condition.notify_all();
      condition.wait(lock, [&] { return cancelled.load(); });
    }
  }
  void wake() noexcept override {
    std::lock_guard<std::mutex> lock{mutex};
    condition.notify_all();
  }
  bool await_wait() {
    std::unique_lock<std::mutex> lock{mutex};
    return condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; });
  }
  std::atomic<std::uint64_t> time{0};
  bool gate{false};
  bool entered{false};
  std::mutex mutex;
  std::condition_variable condition;
};
} // namespace

TEST_CASE("bounded synthetic arrivals conserve generated frames across burst overflow") {
  runtime_baseline::Queue queue;
  queue.add(200);
  CHECK(queue.generated == 200);
  CHECK(queue.dropped == 72);
  CHECK(queue.size() == 128);
  CHECK(queue.maximum_backlog == 128);
  std::uint64_t sequence = 0;
  for (unsigned i = 0; i < 128; ++i) {
    REQUIRE(queue.pop(sequence));
    CHECK(sequence == i);
  }
  CHECK_FALSE(queue.pop(sequence));
  CHECK(queue.generated == queue.consumed + queue.dropped + queue.size());
}

TEST_CASE("ready runtime drains mixed statuses without a source wait checkpoint") {
  ManualPlatform platform;
  platform.gate = true;
  auto config = runtime_baseline::scenarios[1];
  config.frame_limit = 30;
  config.processor_work_us = 7;
  config.observer_work_us = 3;
  runtime_baseline::Fixture fixture{platform, config};
  vehicle_telemetry::Runtime runtime{fixture.source, fixture.processor, fixture.observer};
  REQUIRE(runtime.start().ok());
  const bool reached_limit = platform.await_wait();
  const auto result = runtime.stop();
  REQUIRE(reached_limit);
  REQUIRE(result.ok());
  const auto &metrics = fixture.metrics;
  CHECK(metrics.processed == 10);
  CHECK(metrics.ignored == 10);
  CHECK(metrics.malformed == 10);
  CHECK(metrics.processor_cost.count == 30);
  CHECK(metrics.processor_cost.total_us == 210);
  CHECK(metrics.frame_observer_cost.total_us == 90);
  CHECK(metrics.diagnostic_observer_cost.total_us == 90);
  CHECK(metrics.maximum_frames_between_waits == 30);
  CHECK(metrics.maximum_checkpoint_span_us == 390);
  CHECK(metrics.source_waits == 1);
}

TEST_CASE("burst source records bounded backlog and receive timeout checkpoints") {
  ManualPlatform platform;
  auto config = runtime_baseline::scenarios[2];
  config.initial_frames = 200;
  config.arrival_batch = 200;
  config.arrival_interval_us = 1000;
  runtime_baseline::Fixture fixture{platform, config};
  REQUIRE(fixture.source.start().ok());
  vehicle_core::RawCanFrame frame{};
  for (unsigned i = 0; i < 128; ++i)
    CHECK(fixture.source.receive(frame, 1) == vehicle_telemetry::ReceiveStatus::Frame);
  CHECK(fixture.source.receive(frame, 1) == vehicle_telemetry::ReceiveStatus::Timeout);
  CHECK(fixture.source.receive(frame, 1) == vehicle_telemetry::ReceiveStatus::Frame);
  CHECK(fixture.metrics.source_waits == 1);
  CHECK(fixture.source.queue().dropped == 144);
  CHECK(fixture.source.queue().maximum_backlog == 128);
  CHECK(fixture.source.stop().ok());
}

TEST_CASE("cancellation releases an empty source without processing a frame") {
  ManualPlatform platform;
  platform.gate = true;
  runtime_baseline::Fixture fixture{platform, runtime_baseline::scenarios[4]};
  vehicle_telemetry::Runtime runtime{fixture.source, fixture.processor, fixture.observer};
  REQUIRE(runtime.start().ok());
  const bool waiting = platform.await_wait();
  REQUIRE(runtime.stop().ok());
  REQUIRE(waiting);
  CHECK(fixture.metrics.processor_cost.count == 0);
  CHECK(fixture.metrics.frame_observer_cost.count == 0);
  CHECK(fixture.metrics.maximum_frames_between_waits == 0);
}

TEST_CASE("disabling timing preserves workload counters") {
  ManualPlatform platform;
  platform.gate = true;
  auto config = runtime_baseline::scenarios[3];
  config.frame_limit = 5;
  runtime_baseline::Fixture fixture{platform, config, false};
  vehicle_telemetry::Runtime runtime{fixture.source, fixture.processor, fixture.observer};
  REQUIRE(runtime.start().ok());
  const bool waiting = platform.await_wait();
  REQUIRE(runtime.stop().ok());
  REQUIRE(waiting);
  CHECK(fixture.metrics.processed == 5);
  CHECK(fixture.metrics.processor_cost.count == 0);
  CHECK(fixture.metrics.maximum_frames_between_waits == 5);
  CHECK(fixture.metrics.maximum_checkpoint_span_us == 0);
}

TEST_CASE("stop waits a current slow observer and processes no further frame") {
  class GatedObserverPlatform final : public ManualPlatform {
  public:
    void busy(std::uint32_t us) noexcept override {
      ManualPlatform::busy(us);
      if (us != 200)
        return;
      std::unique_lock<std::mutex> lock{mutex};
      if (!entered) {
        entered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return released; });
      }
    }
    void wake() noexcept override {
      std::lock_guard<std::mutex> lock{mutex};
      cancellation_requested = true;
      condition.notify_all();
    }
    bool await_cancel() {
      std::unique_lock<std::mutex> lock{mutex};
      return condition.wait_for(lock, std::chrono::seconds(2),
                                [&] { return cancellation_requested; });
    }
    void release() {
      std::lock_guard<std::mutex> lock{mutex};
      released = true;
      condition.notify_all();
    }
    bool cancellation_requested{false};
    bool released{false};
  } platform;
  auto config = runtime_baseline::scenarios[0];
  config.observer_work_us = 200;
  runtime_baseline::Fixture fixture{platform, config};
  vehicle_telemetry::Runtime runtime{fixture.source, fixture.processor, fixture.observer};
  vehicle_telemetry::RuntimeConfig work{};
  work.max_frames_per_batch = work.max_runnable_receive_calls = 1;
  work.budget_pause_ms = 1000;
  REQUIRE(runtime.configure(work).ok());
  REQUIRE(runtime.start().ok());
  const bool in_observer = platform.await_wait();
  auto stopping = std::async(std::launch::async, [&] { return runtime.stop(); });
  const bool stop_requested = platform.await_cancel();
  const auto before_release = stopping.wait_for(std::chrono::seconds(0));
  platform.release();
  REQUIRE(stopping.get().ok());
  REQUIRE(in_observer);
  REQUIRE(stop_requested);
  CHECK(before_release == std::future_status::timeout);
  CHECK(fixture.metrics.processed == 1);
  CHECK(fixture.metrics.frame_observer_cost.count == 1);
  CHECK(fixture.metrics.diagnostic_observer_cost.count == 1);
  CHECK(fixture.metrics.maximum_frames_between_waits == 1);
  CHECK(fixture.metrics.maximum_checkpoint_span_us == 410);
  CHECK(fixture.metrics.source_waits == 0);
  CHECK(runtime.diagnostics().work_budget_pauses == 0);
}

TEST_CASE("task residency retains same-task reselections and closes real switches") {
  runtime_baseline::TaskResidency trace;
  trace.select(0, 0, 7, 12, true);
  trace.select(0, 7, 7, 23, true);
  trace.select(0, 7, 7, 33, true);
  trace.select(0, 7, 8, 53, false);
  CHECK(trace.cores[0].maximum_us == 41);
  CHECK(trace.cores[0].segments == 1);
  CHECK(trace.cores[0].reselections == 2);
  CHECK_FALSE(trace.cores[0].active);
}

TEST_CASE("task residency finalizes each core once at run boundary") {
  runtime_baseline::TaskResidency trace;
  trace.select(0, 0, 7, 12, true);
  trace.select(0, 7, 8, 53, false);
  trace.select(1, 0, 7, 83, true);
  trace.finish(200);
  CHECK(trace.cores[0].maximum_us == 41);
  CHECK(trace.cores[1].maximum_us == 117);
  CHECK(trace.cores[1].segments == 1);
  CHECK_FALSE(trace.cores[1].active);
  trace.finish(300);
  CHECK(trace.cores[1].segments == 1);
  CHECK(trace.cores[1].maximum_us == 117);
}

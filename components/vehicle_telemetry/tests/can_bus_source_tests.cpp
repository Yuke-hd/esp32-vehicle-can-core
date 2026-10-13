#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <utility>

#include "can_bus/internal/acquisition_start.hpp"
#include "vehicle_telemetry/can_bus_source.hpp"
#include "vehicle_telemetry/runtime.hpp"

namespace {

// Synthetic operation outcomes only: the production CanBusSource and host
// Runtime run unchanged, but no TWAI driver, FreeRTOS task or board is used.
struct SyntheticBus final {
  can_bus::Result start_result{can_bus::Result::kOk};
  can_bus::LifecycleState state_after_start{can_bus::LifecycleState::kRunning};
  bool retained_on_failure{false};
  can_bus::Result stop_result{can_bus::Result::kOk};
  std::atomic<can_bus::LifecycleState> state{can_bus::LifecycleState::kStopped};
  unsigned starts{0};
  unsigned stops{0};
  std::atomic<unsigned> receives{0};
  std::atomic<unsigned> snapshots{0};
  can_bus::Configuration last_configuration{};
} bus;

struct BusFixture {
  BusFixture() {
    bus.start_result = can_bus::Result::kOk;
    bus.state_after_start = can_bus::LifecycleState::kRunning;
    bus.retained_on_failure = false;
    bus.stop_result = can_bus::Result::kOk;
    bus.state = can_bus::LifecycleState::kStopped;
    bus.starts = bus.stops = 0;
    bus.receives = bus.snapshots = 0;
  }
};

class Processor final : public vehicle_telemetry::FrameProcessor {
public:
  void reset() noexcept override { ++resets; }
  vehicle_telemetry::ProcessResult process(const vehicle_core::RawCanFrame &) noexcept override {
    ++frames;
    return {};
  }
  std::atomic<unsigned> resets{0};
  std::atomic<unsigned> frames{0};
};

class Observer final : public vehicle_telemetry::Observer {
public:
  void on_frame_processed(const vehicle_core::RawCanFrame &,
                          const vehicle_telemetry::ProcessResult &) noexcept override {
    ++frames;
  }
  void on_diagnostics(const vehicle_telemetry::TransportDiagnostics &) noexcept override {
    ++diagnostics;
  }
  std::atomic<unsigned> frames{0};
  std::atomic<unsigned> diagnostics{0};
};

} // namespace

namespace can_bus {

internal::StartOutcome internal::start_with_ownership(const Configuration &configuration) noexcept {
  ++bus.starts;
  bus.last_configuration = configuration;
  bus.state = bus.state_after_start;
  return {bus.start_result, bus.start_result == Result::kOk || bus.retained_on_failure};
}

Result start(const Configuration &configuration) noexcept {
  return internal::start_with_ownership(configuration).result;
}

Result stop() noexcept {
  ++bus.stops;
  if (bus.stop_result == Result::kOk || bus.stop_result == Result::kNotStarted)
    bus.state = LifecycleState::kStopped;
  else
    bus.state = LifecycleState::kFaulted;
  return bus.stop_result;
}

LifecycleState lifecycle() noexcept { return bus.state; }

Result receive(vehicle_core::RawCanFrame &, std::uint32_t) noexcept {
  ++bus.receives;
  return bus.state == LifecycleState::kRunning ? Result::kTimeout : Result::kNotStarted;
}

Statistics statistics(StatisticsOperation) noexcept {
  ++bus.snapshots;
  return {};
}

} // namespace can_bus

TEST_CASE_FIXTURE(BusFixture, "CAN source and borrowed runtime construction are inert") {
  Processor processor;
  Observer observer;
  {
    vehicle_telemetry::CanBusSource source{{500'000, 0}};
    vehicle_telemetry::Runtime runtime{source, processor, observer};
    CHECK(runtime.configure({10, 100'000}).ok());
    CHECK(runtime.lifecycle() == vehicle_telemetry::LifecycleState::Stopped);
    CHECK(runtime.diagnostics().lifecycle == vehicle_telemetry::LifecycleState::Stopped);
  }
  CHECK(bus.starts == 0);
  CHECK(bus.stops == 0);
  CHECK(bus.receives == 0);
  CHECK(bus.snapshots == 0);
  CHECK(processor.resets == 0);
  CHECK(processor.frames == 0);
  CHECK(observer.frames == 0);
  CHECK(observer.diagnostics == 0);
}

TEST_CASE_FIXTURE(BusFixture, "unowned CAN source cleanup preserves an existing acquisition") {
  bus.state = can_bus::LifecycleState::kRunning;
  vehicle_telemetry::CanBusSource source{{500'000, 0}};
  CHECK(source.stop().status == vehicle_telemetry::ResultCode::NotRunning);
  CHECK(source.retry_stop().status == vehicle_telemetry::ResultCode::NotRunning);
  CHECK(bus.stops == 0);
  CHECK(bus.state == can_bus::LifecycleState::kRunning);
}

TEST_CASE_FIXTURE(BusFixture, "runtime cleanup never adopts a conflicting CAN acquisition") {
  SUBCASE("running owner") {
    bus.start_result = can_bus::Result::kAlreadyStarted;
    bus.state_after_start = can_bus::LifecycleState::kRunning;
  }
  SUBCASE("stopping owner") {
    bus.start_result = can_bus::Result::kStopping;
    bus.state_after_start = can_bus::LifecycleState::kStopping;
  }
  SUBCASE("faulted owner") {
    bus.start_result = can_bus::Result::kFaulted;
    bus.state_after_start = can_bus::LifecycleState::kFaulted;
  }
  SUBCASE("external TWAI installation rejects install without acquiring resources") {
    bus.start_result = can_bus::Result::kDriverFailure;
    bus.state_after_start = can_bus::LifecycleState::kStopped;
  }
  Processor processor;
  Observer observer;
  {
    vehicle_telemetry::CanBusSource source{{500'000, 0}};
    vehicle_telemetry::Runtime runtime{source, processor, observer};
    CHECK_FALSE(runtime.start().ok());
    CHECK(runtime.lifecycle() == vehicle_telemetry::LifecycleState::Stopped);
    CHECK(runtime.stop().status == vehicle_telemetry::ResultCode::NotRunning);
  }
  CHECK(bus.starts == 1);
  CHECK(bus.stops == 0);
  CHECK(bus.state == bus.state_after_start);
  CHECK(bus.receives == 0);
}

TEST_CASE_FIXTURE(BusFixture, "runtime retains its own partial CAN startup cleanup for retry") {
  SUBCASE("driver startup failed after installation") {
    bus.start_result = can_bus::Result::kDriverFailure;
  }
  SUBCASE("receive task startup failed after driver start") {
    bus.start_result = can_bus::Result::kTaskFailure;
  }
  bus.state_after_start = can_bus::LifecycleState::kFaulted;
  bus.retained_on_failure = true;
  bus.stop_result = can_bus::Result::kDriverFailure;
  Processor processor;
  Observer observer;
  vehicle_telemetry::CanBusSource source{{500'000, 0}};
  vehicle_telemetry::Runtime runtime{source, processor, observer};
  CHECK(runtime.start().status == vehicle_telemetry::ResultCode::Faulted);
  CHECK(runtime.lifecycle() == vehicle_telemetry::LifecycleState::Faulted);
  CHECK(bus.stops == 1);
  CHECK(bus.receives == 0);

  bus.stop_result = can_bus::Result::kOk;
  CHECK(runtime.stop().ok());
  CHECK(bus.stops == 2);
  CHECK(runtime.lifecycle() == vehicle_telemetry::LifecycleState::Stopped);
  bus.start_result = can_bus::Result::kOk;
  bus.state_after_start = can_bus::LifecycleState::kRunning;
  CHECK(runtime.start().ok());
  CHECK(runtime.stop().ok());
  CHECK(bus.stops == 3);
}

TEST_CASE_FIXTURE(BusFixture, "runtime retries an owned CAN stop and becomes quiescent") {
  Processor processor;
  Observer observer;
  unsigned receives_after_stop = 0;
  unsigned diagnostics_after_stop = 0;
  {
    vehicle_telemetry::CanBusSource source{{500'000, 0}};
    vehicle_telemetry::Runtime runtime{source, processor, observer};
    REQUIRE(runtime.start().ok());
    bus.stop_result = can_bus::Result::kDriverFailure;
    CHECK(runtime.stop().status == vehicle_telemetry::ResultCode::Faulted);
    CHECK(runtime.lifecycle() == vehicle_telemetry::LifecycleState::Faulted);
    bus.stop_result = can_bus::Result::kOk;
    CHECK(runtime.stop().ok());
    CHECK(bus.stops == 2);
    receives_after_stop = bus.receives;
    diagnostics_after_stop = observer.diagnostics;
    CHECK(runtime.stop().status == vehicle_telemetry::ResultCode::NotRunning);
  }
  CHECK(bus.stops == 2);
  CHECK(bus.receives == receives_after_stop);
  CHECK(observer.diagnostics == diagnostics_after_stop);
}

TEST_CASE_FIXTURE(BusFixture, "CAN source copies and assignments never copy cleanup authority") {
  vehicle_telemetry::CanBusSource owner{{500'000, 1}};
  REQUIRE(owner.start().ok());
  vehicle_telemetry::CanBusSource copy{owner};
  vehicle_telemetry::CanBusSource rvalue_copy{std::move(owner)};
  vehicle_telemetry::CanBusSource assigned{{250'000, 2}};
  assigned = owner;
  CHECK(copy.stop().status == vehicle_telemetry::ResultCode::NotRunning);
  CHECK(rvalue_copy.retry_stop().status == vehicle_telemetry::ResultCode::NotRunning);
  CHECK(assigned.stop().status == vehicle_telemetry::ResultCode::NotRunning);
  CHECK(bus.stops == 0);

  // Assigning an unowned source to the owner changes next-start configuration
  // without dropping cleanup responsibility for the current acquisition.
  vehicle_telemetry::CanBusSource replacement{{125'000, 3}};
  owner = replacement;
  auto &same_owner = owner;
  owner = same_owner;
  CHECK(owner.stop().ok());
  CHECK(bus.stops == 1);
  CHECK(owner.stop().status == vehicle_telemetry::ResultCode::NotRunning);
  CHECK(owner.start().ok());
  CHECK(bus.last_configuration.bitrate_bps == 125'000);
  CHECK(bus.last_configuration.bus_id == 3);
  CHECK(owner.stop().ok());
}

TEST_CASE_FIXTURE(BusFixture, "rejected repeated CAN starts preserve the current owner's claim") {
  vehicle_telemetry::CanBusSource owner{{500'000, 0}};
  REQUIRE(owner.start().ok());
  SUBCASE("already running") { bus.start_result = can_bus::Result::kAlreadyStarted; }
  SUBCASE("cleanup still stopping") {
    bus.start_result = can_bus::Result::kStopping;
    bus.state_after_start = can_bus::LifecycleState::kStopping;
  }
  SUBCASE("cleanup faulted") {
    bus.start_result = can_bus::Result::kFaulted;
    bus.state_after_start = can_bus::LifecycleState::kFaulted;
  }
  CHECK_FALSE(owner.start().ok());
  CHECK(owner.stop().ok());
  CHECK(bus.stops == 1);
}

TEST_CASE_FIXTURE(BusFixture, "CAN cleanup releases authority only after completion") {
  vehicle_telemetry::CanBusSource owner{{500'000, 0}};
  REQUIRE(owner.start().ok());
  SUBCASE("driver stop failure") { bus.stop_result = can_bus::Result::kDriverFailure; }
  SUBCASE("task acknowledgement failure") { bus.stop_result = can_bus::Result::kTaskFailure; }
  SUBCASE("timeout") { bus.stop_result = can_bus::Result::kTimeout; }
  SUBCASE("stopping") { bus.stop_result = can_bus::Result::kStopping; }
  CHECK_FALSE(owner.stop().ok());
  bus.stop_result = can_bus::Result::kNotStarted;
  CHECK(owner.retry_stop().status == vehicle_telemetry::ResultCode::NotRunning);
  CHECK(bus.stops == 2);
  CHECK(owner.retry_stop().status == vehicle_telemetry::ResultCode::NotRunning);
  CHECK(bus.stops == 2);
}

TEST_CASE_FIXTURE(BusFixture, "later CAN ownership cannot change a completed startup outcome") {
  // This attempt failed and finished cleanup. Before adapter bookkeeping,
  // another application's start publishes Running. The returned attempt
  // outcome must remain unowned despite that newer global lifecycle.
  bus.start_result = can_bus::Result::kDriverFailure;
  bus.retained_on_failure = false;
  bus.state_after_start = can_bus::LifecycleState::kRunning;
  Processor processor;
  Observer observer;
  vehicle_telemetry::CanBusSource source{{500'000, 0}};
  vehicle_telemetry::Runtime runtime{source, processor, observer};
  CHECK(runtime.start().status == vehicle_telemetry::ResultCode::Faulted);
  CHECK(runtime.lifecycle() == vehicle_telemetry::LifecycleState::Stopped);
  CHECK(source.stop().status == vehicle_telemetry::ResultCode::NotRunning);
  CHECK(bus.stops == 0);
  CHECK(bus.state == can_bus::LifecycleState::kRunning);
}

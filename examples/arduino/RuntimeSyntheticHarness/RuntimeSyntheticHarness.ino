// SYNTHETIC ONLY: no TWAI initialization, driver binding, CAN pins, or CAN traffic.
#include <Arduino.h>
#include <ESP32VehicleCANCore.h>
#include <esp_timer.h>
#include <atomic>

namespace {
using namespace vehicle_telemetry;
constexpr unsigned long kDeadlineMs = 500;
enum class Input { Silence, Frames, OneFrame, Fault };

class SyntheticSource final : public AcquisitionSource {
public:
  // Set these only after checked stop/quiescence. Counters cross tasks atomically.
  Input input{Input::Silence};
  bool fail_start{false};
  bool fail_stop_once{false};
  std::atomic<bool> running{false};
  std::atomic<std::uint32_t> calls{0}, frames{0}, stops{0}, retries{0};
  StatusResult start() noexcept override {
    calls = 0;
    frames = 0;
    stops = 0;
    retries = 0;
    if (fail_start)
      return {ResultCode::Faulted};
    running = true;
    return {};
  }
  StatusResult stop() noexcept override {
    running = false; // Cancellation requested even when cleanup reports failure.
    ++stops;
    return {fail_stop_once ? ResultCode::Timeout : ResultCode::Ok};
  }
  bool supports_stop_retry() const noexcept override { return true; }
  StatusResult retry_stop() noexcept override {
    running = false;
    ++retries;
    return {};
  }
  ReceiveStatus receive(vehicle_core::RawCanFrame &frame, std::uint32_t timeout_ms) noexcept override {
    ++calls;
    if (!running.load())
      return ReceiveStatus::NotStarted;
    if (input == Input::Fault)
      return ReceiveStatus::Fault;
    if (input == Input::Frames || (input == Input::OneFrame && frames.load() == 0)) {
      frame = {}; // Generic synthetic frame, never printed or decoded.
      ++frames;
      return ReceiveStatus::Frame;
    }
    delay(timeout_ms); // Finite silence polling; no busy wait.
    return ReceiveStatus::Timeout;
  }
  AcquisitionStatistics statistics() const noexcept override {
    AcquisitionStatistics result{};
    result.frames_received = frames.load();
    return result;
  }
};

class SyntheticProcessor final : public FrameProcessor {
public:
  bool fail{false}; // Stopped-only configuration.
  std::atomic<std::uint32_t> processed{0};
  void reset() noexcept override { processed = 0; }
  ProcessResult process(const vehicle_core::RawCanFrame &) noexcept override {
    ++processed;
    return {fail ? ProcessStatus::Fault : ProcessStatus::Processed};
  }
};

class SyntheticObserver final : public Observer {
public:
  unsigned long delay_ms{0}; // Stopped-only configuration; always finite.
  std::atomic<std::uint32_t> completed{0}, diagnostic_calls{0}, max_us{0};
  std::atomic<bool> active{false};
  void reset() {
    completed = 0;
    diagnostic_calls = 0;
    max_us = 0;
    active = false;
  }
  void on_frame_processed(const vehicle_core::RawCanFrame &, const ProcessResult &) noexcept override {
    const auto began = esp_timer_get_time();
    active = true;
    if (delay_ms != 0)
      delay(delay_ms); // Worker callback remains in flight; budgets cannot preempt it.
    const auto elapsed = static_cast<std::uint32_t>(esp_timer_get_time() - began);
    // Only this worker writes the maximum; the application only reads it.
    if (elapsed > max_us.load())
      max_us = elapsed;
    ++completed;
    active = false;
  }
  void on_diagnostics(const TransportDiagnostics &) noexcept override { ++diagnostic_calls; }
};

// Dependency construction precedes Runtime, destruction follows it. No borrowed stack contexts.
SyntheticSource source;
SyntheticProcessor processor;
SyntheticObserver observer;
Runtime runtime{source, processor, observer};
std::uint32_t checks = 0, failures = 0;
bool finished = false;

void check(const char *id, bool passed) {
  ++checks;
  if (!passed)
    ++failures;
  const auto d = runtime.diagnostics();
  Serial.printf("SYNTHETIC %s %s lifecycle=%u transport=%u calls=%lu callbacks=%lu "
                "callback_max_us=%lu budget_pauses=%llu\n", id, passed ? "PASS" : "FAIL",
                static_cast<unsigned>(d.lifecycle), static_cast<unsigned>(d.transport),
                static_cast<unsigned long>(source.calls.load()),
                static_cast<unsigned long>(observer.completed.load()),
                static_cast<unsigned long>(observer.max_us.load()), d.work_budget_pauses);
}

template <typename Predicate> bool wait_for(Predicate predicate) {
  const auto began = millis();
  while (!predicate() && millis() - began < kDeadlineMs)
    delay(1);
  return predicate();
}

bool stop_checked() {
  const auto result = runtime.stop();
  return (result.ok() || result.status == ResultCode::NotRunning) &&
         runtime.lifecycle() == LifecycleState::Stopped;
}

bool begin_case(Input input, unsigned long observer_delay = 0, bool processor_fault = false,
                bool stop_fault = false) {
  // Never modify borrowed dependencies after failed cleanup.
  if (!stop_checked())
    return false;
  source.input = input;
  source.fail_start = false;
  source.fail_stop_once = stop_fault;
  processor.fail = processor_fault;
  observer.delay_ms = observer_delay;
  observer.reset();
  RuntimeConfig config{};
  config.receive_timeout_ms = 5;
  config.transport_silence_timeout_us = 20'000;
  return runtime.configure(config).ok() && runtime.start().ok();
}

void run_checks() {
  RuntimeConfig invalid{};
  invalid.receive_timeout_ms = 0;
  check("invalid_configuration", runtime.configure(invalid).status == ResultCode::InvalidConfiguration);
  source.fail_start = true;
  check("start_failure", runtime.start().status == ResultCode::Faulted && stop_checked());

  if (!begin_case(Input::Silence))
    return;
  const bool polled = wait_for([] { return observer.diagnostic_calls.load() >= 3; });
  delay(30); // Longer than silence policy; pre-traffic state is still AwaitingTraffic.
  check("initial_silence", polled && runtime.diagnostics().transport ==
                                      vehicle_core::TransportHealth::AwaitingTraffic &&
                              !runtime.diagnostics().has_last_frame && observer.completed.load() == 0);
  if (!begin_case(Input::OneFrame))
    return;
  const bool timed_out = wait_for([] {
    return runtime.diagnostics().transport == vehicle_core::TransportHealth::TimedOut;
  });
  check("frame_then_silence", timed_out && runtime.diagnostics().has_last_frame &&
                                 observer.completed.load() == 1);

  if (!begin_case(Input::Frames))
    return;
  const auto before = observer.completed.load();
  delay(80); // Deliberately delay Arduino loop; no Runtime pump occurs here.
  const auto d = runtime.diagnostics();
  check("delayed_loop_bounded_observer", observer.completed.load() > before + 16 &&
                                             d.work_budget_pauses > 0);
  check("configure_running_rejected", runtime.configure({}).status == ResultCode::InvalidState);

  if (!begin_case(Input::Frames, 35))
    return;
  const bool entered = wait_for([] { return observer.active.load(); });
  const auto calls = source.calls.load();
  const auto completed = observer.completed.load();
  const auto pauses = runtime.diagnostics().work_budget_pauses;
  delay(5);
  const bool in_flight = entered && observer.active.load() && source.calls.load() == calls &&
                         observer.completed.load() == completed &&
                         runtime.diagnostics().work_budget_pauses == pauses;
  const bool returned = wait_for([] {
    return observer.completed.load() > 0 && runtime.diagnostics().work_budget_pauses > 0;
  });
  check("observer_cooperative_nonpreemption", in_flight && returned && observer.max_us.load() >= 25'000);

  if (!begin_case(Input::Fault))
    return;
  const bool receive_fault = wait_for([] { return runtime.lifecycle() == LifecycleState::Faulted; });
  check("receive_fault", receive_fault &&
                             runtime.diagnostics().transport == vehicle_core::TransportHealth::Faulted);
  if (!begin_case(Input::Frames, 0, true))
    return;
  const bool process_fault = wait_for([] { return runtime.lifecycle() == LifecycleState::Faulted; });
  check("processor_fault", process_fault && runtime.diagnostics().processor_faults == 1);

  if (!begin_case(Input::Frames, 0, false, true))
    return;
  (void)wait_for([] { return observer.completed.load() > 0; });
  const auto failed_stop = runtime.stop();
  check("failed_stop_retains_context", failed_stop.status == ResultCode::Timeout &&
                                          runtime.lifecycle() == LifecycleState::Stopping &&
                                          runtime.start().status == ResultCode::Stopping);
  const bool retried = stop_checked();
  const auto stopped_callbacks = observer.completed.load();
  const auto stopped_calls = source.calls.load();
  delay(20);
  check("stop_retry_quiescence", retried && source.stops.load() == 1 && source.retries.load() == 1 &&
                                     !observer.active.load() && observer.completed.load() == stopped_callbacks &&
                                     source.calls.load() == stopped_calls);
  if (!begin_case(Input::Frames))
    return;
  const bool restarted = wait_for([] { return observer.completed.load() > 0; });
  check("restart_after_quiescence", restarted && stop_checked());
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("SYNTHETIC Runtime harness; no CAN/TWAI initialized; checks run once from loop");
  const auto result = runtime.configure({});
  check("setup_configure", result.ok());
}

void loop() {
  if (!finished) {
    run_checks();
    const bool stopped = stop_checked();
    check("final_cleanup", stopped);
    // A failed preparation aborts remaining cases; count makes omissions observable.
    Serial.printf("SYNTHETIC SUMMARY checks=%lu expected=14 failures=%lu result=%s\n",
                  static_cast<unsigned long>(checks), static_cast<unsigned long>(failures),
                  checks == 14 && failures == 0 ? "PASS" : "FAIL");
    finished = true; // Keep all static dependencies alive even if cleanup failed.
  }
  delay(100);
}

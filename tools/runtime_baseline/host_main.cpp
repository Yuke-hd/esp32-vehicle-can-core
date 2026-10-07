#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "report.hpp"

namespace {
class HostPlatform final : public runtime_baseline::Platform {
public:
  std::uint64_t now() const noexcept override {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
  }
  void busy(std::uint32_t us) noexcept override {
    if (us == 0)
      return;
    const auto until = now() + us;
    while (now() < until) {
    }
  }
  void wait(std::uint32_t us, const std::atomic<bool> &cancelled) noexcept override {
    std::unique_lock<std::mutex> lock{mutex_};
    condition_.wait_for(lock, std::chrono::microseconds(us), [&] { return cancelled.load(); });
  }
  void wake() noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    condition_.notify_all();
  }

private:
  std::mutex mutex_;
  std::condition_variable condition_;
};
} // namespace

int main() {
  std::printf("metadata,host,compiler=%s,diagnostics=both_callbacks,receive_timeout_ms=1,"
              "duration_ms=250,repeats=3,build_type=%s,wall_timing_only=1\n",
              BASELINE_COMPILER_VERSION, BASELINE_BUILD_TYPE);
  runtime_baseline::print_header();
  for (const auto &scenario : runtime_baseline::scenarios) {
    for (bool timing : {false, true}) {
      for (unsigned repeat = 0; repeat < 3; ++repeat) {
        HostPlatform platform;
        runtime_baseline::Fixture fixture{platform, scenario, timing};
        vehicle_telemetry::Runtime runtime{fixture.source, fixture.processor, fixture.observer};
        vehicle_telemetry::RuntimeConfig config{};
        config.receive_timeout_ms = 1;
        if (!runtime.configure(config).ok())
          return 1;
        const auto started = platform.now();
        if (!runtime.start().ok())
          return 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const auto stopping = platform.now();
        if (!runtime.stop().ok())
          return 1;
        const auto stopped = platform.now();
        runtime_baseline::print_run("host", scenario, repeat, fixture,
                                    {stopped - started, stopped - stopping, {}, {}});
      }
    }
  }
}

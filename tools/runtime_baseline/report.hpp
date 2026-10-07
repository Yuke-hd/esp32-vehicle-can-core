#pragma once

#include <cstdio>

#include "fixture.hpp"

namespace runtime_baseline {
struct Run final {
  std::uint64_t elapsed_us{0}, stop_us{0};
  std::array<std::uint32_t, 2> idle_calls{};
  std::array<std::uint32_t, 2> maximum_idle_gap_us{};
};
inline void print_header() {
  std::puts(
      "record,platform,scenario,repeat,timing,initial,batch,interval_us,processor_work_us,"
      "observer_work_us,frame_limit,run_us,stop_us,frames_per_second,processed,ignored,malformed,"
      "generated,consumed,dropped,backlog,max_backlog,source_waits,max_frames_between_waits,"
      "max_checkpoint_wall_span_us,receive_calls,receive_total_us,receive_max_us,"
      "wait_calls,wait_total_us,wait_max_us,process_calls,process_total_us,process_max_us,"
      "frame_observer_calls,frame_observer_total_us,frame_observer_max_us,"
      "diagnostic_calls,diagnostic_total_us,diagnostic_max_us,diagnostic_processed,"
      "diagnostic_ignored,diagnostic_malformed,running_core0,running_core1,"
      "affinity_core0,affinity_core1,idle0,idle1,max_idle_gap0_us,max_idle_gap1_us");
}
inline void print_run(const char *platform, const Scenario &scenario, unsigned repeat,
                      const Fixture &fixture, const Run &run) {
  const auto &m = fixture.metrics;
  const auto &q = fixture.source.queue();
  std::printf("baseline,%s,%s,%u,%u,%u,%u,%u,%u,%u,%u", platform, scenario.name, repeat,
              m.timing ? 1U : 0U, static_cast<unsigned>(scenario.initial_frames),
              static_cast<unsigned>(scenario.arrival_batch),
              static_cast<unsigned>(scenario.arrival_interval_us),
              static_cast<unsigned>(scenario.processor_work_us),
              static_cast<unsigned>(scenario.observer_work_us),
              static_cast<unsigned>(scenario.frame_limit));
  const auto value = [](std::uint64_t n) {
    std::printf(",%llu", static_cast<unsigned long long>(n));
  };
  value(run.elapsed_us);
  value(run.stop_us);
  value(run.elapsed_us ? m.frames() * 1'000'000 / run.elapsed_us : 0);
  for (auto n :
       {m.processed, m.ignored, m.malformed, q.generated, q.consumed, q.dropped,
        static_cast<std::uint64_t>(q.size()), static_cast<std::uint64_t>(q.maximum_backlog),
        m.source_waits, m.maximum_frames_between_waits, m.maximum_checkpoint_span_us})
    value(n);
  for (const auto &cost : {m.receive_cost, m.source_wait_cost, m.processor_cost,
                           m.frame_observer_cost, m.diagnostic_observer_cost}) {
    value(cost.count);
    value(cost.total_us);
    value(cost.maximum_us);
  }
  for (auto n :
       {m.last_diagnostics.frames_processed, m.last_diagnostics.frames_ignored,
        m.last_diagnostics.frames_malformed, m.running_core_after_float[0],
        m.running_core_after_float[1], m.affinity_after_float[0], m.affinity_after_float[1]})
    value(n);
  for (auto n : run.idle_calls)
    value(n);
  for (auto n : run.maximum_idle_gap_us)
    value(n);
  std::puts("");
}
} // namespace runtime_baseline

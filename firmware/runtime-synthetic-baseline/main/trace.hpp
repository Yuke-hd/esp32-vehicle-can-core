#pragma once

#include <cstdint>

#include "task_residency.hpp"

void baseline_trace_begin() noexcept;
runtime_baseline::TaskResidency baseline_trace_end() noexcept;
std::uint32_t baseline_watchdog_events() noexcept;

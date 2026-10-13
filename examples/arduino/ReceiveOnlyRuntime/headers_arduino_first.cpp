// Separate translation unit: Arduino preprocessing cannot insert Arduino.h here.
#include <Arduino.h>

#include <ESP32VehicleCANCore.h>
#include <can_bus/can_bus.h>
#include <can_bus/driver_binding.hpp>
#include <vehicle_core/decoder_contracts.hpp>
#include <vehicle_core/frame.hpp>
#include <vehicle_core/health.hpp>
#include <vehicle_core/notification.hpp>
#include <vehicle_core/notification_channel.hpp>
#include <vehicle_core/reading.hpp>
#include <vehicle_core/signal.hpp>
#include <vehicle_core/telemetry_contracts.hpp>
#include <vehicle_core/time.hpp>
#include <vehicle_core/vehicle_core.hpp>
#include <vehicle_telemetry/can_bus_source.hpp>
#include <vehicle_telemetry/lifecycle.hpp>
#include <vehicle_telemetry/observer.hpp>
#include <vehicle_telemetry/receive.hpp>
#include <vehicle_telemetry/runtime.hpp>

static_assert(__cplusplus == 201703L, "Example requires the selected C++17 profile");
#ifndef ESP_PLATFORM
#error Expected the ESP32 runtime backend
#endif

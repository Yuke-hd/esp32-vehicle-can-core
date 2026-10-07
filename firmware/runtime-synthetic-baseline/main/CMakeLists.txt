idf_component_register(
  SRCS "main.cpp" "trace.cpp" "../../../components/vehicle_telemetry/src/runtime.cpp"
  INCLUDE_DIRS "../../../tools/runtime_baseline"
               "../../../components/vehicle_telemetry/include"
               "../../../components/vehicle_core/include"
  REQUIRES esp_timer esp_system freertos
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)

if(CONFIG_BASELINE_SCHEDULER_TRACE)
  target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=vTaskSwitchContext")
endif()

if(COMMAND idf_component_register)
  idf_component_register(
    SRCS "src/runtime.cpp" "src/can_bus_source.cpp"
    INCLUDE_DIRS "include"
    REQUIRES vehicle_core can_bus
  )
  return()
endif()

add_library(vehicle_telemetry STATIC
  src/runtime.cpp
)

find_package(Threads REQUIRED)
target_include_directories(vehicle_telemetry
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(vehicle_telemetry PUBLIC vehicle_core Threads::Threads)
target_compile_features(vehicle_telemetry PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(vehicle_telemetry PRIVATE /W4 /permissive-)
else()
  target_compile_options(vehicle_telemetry PRIVATE -Wall -Wextra -Wpedantic)
endif()

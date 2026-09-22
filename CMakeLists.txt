# CMake 3.20 is the supported host minimum. The policy range keeps modern
# CMake releases (including CMake 4.x) on the policies this project tested.
cmake_minimum_required(VERSION 3.20...4.4)

project(esp32_vehicle_can_core_host LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VEHICLE_CAN_CORE_BUILD_HOST_TESTS "Build portable vehicle CAN core host tests" ${BUILD_TESTING})

add_subdirectory(lib/vehicle_core)

if(VEHICLE_CAN_CORE_BUILD_HOST_TESTS)
  enable_testing()
  add_subdirectory(tests/host)
endif()

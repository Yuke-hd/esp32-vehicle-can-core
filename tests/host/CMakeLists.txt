include(FetchContent)

# Keep the host test dependency reproducible. Upgrade only in a dedicated change.
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  # v2.5.0 raises doctest's CMake minimum to 3.14, which remains compatible
  # with the project's 3.20 minimum and modern CMake 4.x.
  GIT_TAG d44d4f6e66232d716af82f00a063759e9d0e50d6
)
FetchContent_MakeAvailable(doctest)

add_executable(vehicle_core_tests
  vehicle_core_tests.cpp
)
target_link_libraries(vehicle_core_tests PRIVATE vehicle_core doctest::doctest)
target_compile_features(vehicle_core_tests PRIVATE cxx_std_17)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(vehicle_core_tests)

add_executable(can_bus_boundary_tests can_bus_boundary_tests.cpp)
target_include_directories(
  can_bus_boundary_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/components/can_bus/include
    ${CMAKE_SOURCE_DIR}/components/can_bus/private_include
    ${CMAKE_SOURCE_DIR}/lib/vehicle_core/include
)
target_link_libraries(can_bus_boundary_tests PRIVATE doctest::doctest)
target_compile_features(can_bus_boundary_tests PRIVATE cxx_std_17)
doctest_discover_tests(can_bus_boundary_tests)

add_executable(can_bus_lifecycle_tests can_bus_lifecycle_tests.cpp)
target_include_directories(
  can_bus_lifecycle_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/components/can_bus/include
    ${CMAKE_SOURCE_DIR}/components/can_bus/private_include
    ${CMAKE_SOURCE_DIR}/lib/vehicle_core/include
)
target_compile_features(can_bus_lifecycle_tests PRIVATE cxx_std_17)
target_link_libraries(can_bus_lifecycle_tests PRIVATE doctest::doctest)
doctest_discover_tests(can_bus_lifecycle_tests)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(
  NAME architecture_contracts
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/check_architecture.py
          --root ${CMAKE_SOURCE_DIR}
          --compiler ${CMAKE_CXX_COMPILER}
          --cmake ${CMAKE_COMMAND}
)

add_test(
  NAME architecture_checker_regression
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tests/tools/check_architecture_test.py
)

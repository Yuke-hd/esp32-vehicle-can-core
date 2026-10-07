cmake_minimum_required(VERSION 3.16)

# Software-only runtime bench: no CAN driver component or board binding.
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(runtime_synthetic_baseline)

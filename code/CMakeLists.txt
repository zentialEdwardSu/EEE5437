cmake_minimum_required(VERSION 3.25)

project(DigitalImageCompression LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectConventions)

enable_testing()

dic_register_libraries("${CMAKE_CURRENT_SOURCE_DIR}/lib")
dic_register_homeworks("${CMAKE_CURRENT_SOURCE_DIR}")
dic_register_tests("${CMAKE_CURRENT_SOURCE_DIR}/tests")

dic_create_aggregate_target(homeworks DIC_HOMEWORK_BUILD_TARGETS)
dic_create_aggregate_target(project_tests DIC_TEST_BUILD_TARGETS)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TargetOverrides.cmake" OPTIONAL)
if(COMMAND dic_apply_target_overrides)
    dic_apply_target_overrides()
endif()

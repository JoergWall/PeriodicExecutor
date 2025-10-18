cmake_minimum_required(VERSION 3.10)

project(PeriodicExecutor VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

if (POLICY CMP0167)
      cmake_policy(SET CMP0167 NEW)
endif()
# for macOS boost installed with Homebrew
if(APPLE)
    message("Configuring for macOS with Homebrew Boost")
else()
    message("Configuring for non-macOS system")
endif()
find_package(Boost REQUIRED COMPONENTS thread chrono unit_test_framework)
message(STATUS "Boost_FOUND = ${Boost_FOUND}")
message(STATUS "Boost_VERSION = ${Boost_VERSION_STRING}")
message(STATUS "Boost_INCLUDE_DIRS = ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost_LIBRARIES = ${Boost_LIBRARIES}")

# --- BEGIN MANUAL FIND ---
# 1. Find headers
# sometimes CMake's FindBoost has issues, as libs come with suffix -mt or -mt-x on e.g. macOS
# therefore we do a manual find here
find_path(Boost_INCLUDE_DIRS
  NAMES boost/version.hpp
  PATH_SUFFIXES include
  NO_DEFAULT_PATH
  REQUIRED
)
# 2. Find individual libraries
find_library(Boost_THREAD_LIBRARY NAMES 
  boost_thread 
  boost_thread-mt
  boost_thread-mt-x
  thread-mt
  REQUIRED
)
find_library(Boost_CHRONO_LIBRARY NAMES 
  boost_chrono 
  boost_chrono-mt
  boost_chrono-mt-x
  chrono-mt
  REQUIRED
)
find_library(Boost_UNIT_TEST_FRAMEWORK_LIBRARY NAMES 
  boost_unit_test_framework 
  unit_test_framework-mt
  REQUIRED
)
# 3. Combine them into the required legacy variable
set(Boost_LIBRARIES 
  ${Boost_THREAD_LIBRARY}
  ${Boost_CHRONO_LIBRARY}
  ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}
)
# --- END MANUAL FIND ---

# Find Threads
find_package(Threads REQUIRED)

# Include directories
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/include)

# Header-only library target
add_library(PeriodicExecutor INTERFACE)
target_include_directories(PeriodicExecutor INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(PeriodicExecutor INTERFACE
    ${Boost_LIBRARIES}
)


# Build examples
option(BUILD_EXAMPLES "Build examples" ON)
if(BUILD_EXAMPLES)
    # example_PE01
    add_executable(example_PE01 examples/example_PE01.cpp)
    target_link_libraries(example_PE01 PRIVATE PeriodicExecutor)
    # benchmark 
    add_executable(benchmark examples/benchmark.cpp)
    target_link_libraries(benchmark PRIVATE PeriodicExecutor)
endif() # examples
# Build tests
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    # periodic_executor_test
    add_executable(periodic_executor_test tests/PeriodicExecutorTests.cpp)
    target_link_libraries(periodic_executor_test PRIVATE PeriodicExecutor)
endif() # tests

# Doxygen documentation generation
find_package(Doxygen)
if (DOXYGEN_FOUND)
    set(DOXYGEN_INPUT_FILES 
        ${CMAKE_CURRENT_SOURCE_DIR}/include/PeriodicExecutor.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/PeriodicExecutorTests.cpp
    )
    doxygen_add_docs(doxygen_docs ${DOXYGEN_INPUT_FILES})
endif()


# run in build directory
# ----------------------
# make -j4

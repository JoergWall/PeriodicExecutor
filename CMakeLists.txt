cmake_minimum_required(VERSION 3.10)

project(PeriodicExecutor  VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

if (POLICY CMP0167)
  # Use the modern, Boost-provided configuration
  cmake_policy(SET CMP0167 NEW)
endif()
# Find Boost libraries
find_package(Boost 1.74.0 REQUIRED COMPONENTS system thread chrono REQUIRED)


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
    Boost::system 
    Boost::thread      
    Boost::chrono
    # pthread           # For POSIX threading support on Linux
)

# Build examples
option(BUILD_EXAMPLES "Build examples" ON)
if(BUILD_EXAMPLES)
    # example_PE01
    add_executable(example_PE01 examples/example_PE01.cpp)
    target_link_libraries(example_PE01
    Boost::system
    Boost::thread
    Boost::chrono
    )
    # benchmark 
    add_executable(benchmark examples/benchmark.cpp)
    target_link_libraries(benchmark
    Boost::system
    Boost::thread
    Boost::chrono
    )
    find_package(Boost 1.74.0 COMPONENTS system thread unit_test_framework REQUIRED)
    add_executable(periodic_executor_test examples/PeriodicExecutorTests.cpp)
    target_link_libraries(periodic_executor_test
    Boost::system
    Boost::thread
    Boost::unit_test_framework
    pthread
    )
endif()


# Doxygen documentation generation
find_package(Doxygen)
if (DOXYGEN_FOUND)
    doxygen_add_docs(doxygen_docs ${CMAKE_CURRENT_SOURCE_DIR}/PeriodicExecutor.hpp ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
endif()


# run in build directory
# ----------------------
# make -j4

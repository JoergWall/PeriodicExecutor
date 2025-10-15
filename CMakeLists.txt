cmake_minimum_required(VERSION 3.10)

project(PeriodicExecutor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

if (POLICY CMP0167)
  # Use the modern, Boost-provided configuration
  cmake_policy(SET CMP0167 NEW)
endif()
# Find Boost libraries
find_package(Boost 1.74.0 COMPONENTS system thread REQUIRED)

# Include directories
include_directories(${Boost_INCLUDE_DIRS})
include_directories(.) # For PeriodicExecutor.hpp

# Add the executable
add_executable(example_PE01 main.cpp)

# Link Boost libraries
target_link_libraries(example_PE01
    Boost::system
    Boost::thread
)

# Doxygen documentation generation
find_package(Doxygen)
if (DOXYGEN_FOUND)
    doxygen_add_docs(doxygen_docs ${CMAKE_CURRENT_SOURCE_DIR}/PeriodicExecutor.hpp ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
endif()

# --- Unit Testing Configuration ---

# 1. Ensure 'unit_test_framework' is requested in find_package(Boost)
# Assuming your existing line looks like this:
# find_package(Boost 1.74.0 COMPONENTS system thread REQUIRED)
# Change it to:
find_package(Boost 1.74.0 COMPONENTS system thread unit_test_framework REQUIRED)

# 2. Add the test executable
# NOTE: The PeriodicExecutor.hpp header includes PeriodicExecutor.ipp, 
# so we only need to specify the test source file here.
add_executable(periodic_executor_test PeriodicExecutorTests.cpp)

# 3. Link the necessary Boost libraries to the test executable
target_link_libraries(periodic_executor_test
    Boost::system
    Boost::thread
    Boost::unit_test_framework
)

# 4. Integrate with CTest
include(CTest)
add_test(NAME PeriodicExecutorUnitTest COMMAND periodic_executor_test)



# run in build directory
# ctest --verbose
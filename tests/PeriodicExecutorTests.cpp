#define BOOST_TEST_MODULE PeriodicExecutorTestModule
// Use the header-only version of Boost Test for simpler compilation
#include <boost/test/included/unit_test.hpp>

#include "PeriodicExecutor.hpp" // Include the component under test
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

using namespace std::chrono_literals;

// Define a safe margin for timer tolerance (e.g., 20% of the period)
// Due to OS scheduling and thread context switching, exact timing is impossible.
constexpr std::chrono::milliseconds TIME_TOLERANCE = 50ms;

/**
 * @defgroup TestSuite PeriodicExecutor Unit Tests
 * @brief Test cases for verifying the functionality, timing, and lifecycle of PeriodicExecutor.
 * @{
 */

/**
 * @brief Test Fixture for initializing and cleaning up common resources.
 *
 * @details The fixture is not strictly necessary for these simple tests, but
 * it provides a structured environment for setting up objects like the `io_context`
 * if we were using an external executor. For simplicity, we use the default
 * executor in the tests.
 */
struct ExecutorFixture {
    ExecutorFixture() {}
    ~ExecutorFixture() {}
};

/**
 * @brief Tests basic start and stop functionality.
 *
 * @details Verifies that a task can be successfully started and that it executes
 * at least once before being stopped. Also ensures that `stop()` is graceful.
 */
BOOST_FIXTURE_TEST_SUITE(PeriodicExecutorTests, ExecutorFixture)

BOOST_AUTO_TEST_CASE(Test_01_StartAndStop) {
    PeriodicExecutor<> executor;
    std::atomic<int> count{0};
    
    // Task with a relatively long interval
    const auto interval = 200ms;
    
    BOOST_TEST_MESSAGE("Starting executor for Test 01...");
    
    executor.start(interval, [&]() {
        count++;
    });

    // Let the task run for a short period
    std::this_thread::sleep_for(450ms);
    
    BOOST_TEST_MESSAGE("Stopping executor...");
    executor.stop();
    
    // Expect the task to have run at least 2 times (450ms / 200ms = 2.25)
    BOOST_CHECK_GE(count.load(), 2);
    
    BOOST_TEST_MESSAGE("Test 01 finished. Final count: " << count.load());
}

/**
 * @brief Tests the accuracy of the periodic timing.
 *
 * @details Verifies that the number of executions falls within an acceptable
 * range based on the run duration and the set interval.
 */
BOOST_AUTO_TEST_CASE(Test_02_TimingAccuracy) {
    PeriodicExecutor<> executor;
    std::atomic<int> count{0};
    
    const auto interval = 100ms;
    const auto duration = 1000ms; // 1 second
    const int expected_runs = 10;
    
    // We allow a large tolerance margin for safety in continuous integration environments
    const int min_runs = expected_runs - 2; // Allowing 8 to 12 runs
    const int max_runs = expected_runs + 2;
    
    BOOST_TEST_MESSAGE("Starting timing accuracy test (100ms interval for 1s)...");
    
    executor.start(interval, [&]() {
        count++;
    });

    std::this_thread::sleep_for(duration + interval); // Run for 1s plus one interval
    
    executor.stop();
    
    BOOST_CHECK_GE(count.load(), min_runs);
    BOOST_CHECK_LE(count.load(), max_runs);
    
    BOOST_TEST_MESSAGE("Test 02 finished. Actual count: " << count.load() << 
                       ", Expected range: [" << min_runs << ", " << max_runs << "]");
}

/**
 * @brief Tests the Pause and Resume functionality.
 *
 * @details Verifies that `pause()` prevents further execution and that `resume()`
 * restarts the timing loop correctly.
 */
BOOST_AUTO_TEST_CASE(Test_03_PauseResumeFunctionality) {
    PeriodicExecutor<> executor;
    std::atomic<int> count{0};
    
    const auto interval = 100ms;
    
    executor.start(interval, [&]() {
        count++;
    });

    // 1. Run for a baseline (500ms -> Expected 5 runs)
    std::this_thread::sleep_for(500ms + TIME_TOLERANCE);
    const int count_before_pause = count.load();
    
    BOOST_TEST_MESSAGE("Pausing executor. Count before pause: " << count_before_pause);
    
    // 2. Pause and wait for a long time (500ms -> 0 runs expected)
    executor.pause();
    std::this_thread::sleep_for(500ms);
    
    const int count_after_pause = count.load();
    // Check that the count did not increase during the pause period
    BOOST_CHECK_EQUAL(count_after_pause, count_before_pause);
    
    BOOST_TEST_MESSAGE("Resuming executor. Count stabilized at: " << count_after_pause);
    
    // 3. Resume and wait again (500ms -> Expected 5 more runs)
    executor.resume();
    std::this_thread::sleep_for(500ms + TIME_TOLERANCE);
    
    executor.stop();
    
    const int final_count = count.load();
    const int expected_total_min = count_before_pause + 3; // Allowing 3 additional runs
    
    BOOST_CHECK_GE(final_count, expected_total_min);
    BOOST_CHECK_LE(final_count, count_before_pause + 7); // Allowing up to 7 additional runs
    
    BOOST_TEST_MESSAGE("Test 03 finished. Final count: " << final_count);
}

/**
 * @brief Tests that methods are safe to call multiple times (idempotency).
 *
 * @details Ensures calling start/stop/pause/resume consecutively does not crash
 * the application or lead to inconsistent state.
 */
BOOST_AUTO_TEST_CASE(Test_04_IdempotencyAndSafety) {
    PeriodicExecutor<> executor;
    std::atomic<int> count{0};
    
    // Start sequence
    executor.start(100ms, [&]() { count++; });
    BOOST_CHECK_EQUAL(count.load(), 0); // Task hasn't run yet
    
    // Attempt to start again (should return false and not crash)
    BOOST_CHECK_EQUAL(executor.start(10ms, [&]() { count++; }), false);
    
    // Pause/Resume sequence
    executor.pause();
    executor.pause(); // Idempotent call
    
    std::this_thread::sleep_for(200ms);
    BOOST_CHECK_EQUAL(count.load(), 0); // Still paused
    
    executor.resume();
    executor.resume(); // Idempotent call
    
    std::this_thread::sleep_for(300ms);
    BOOST_CHECK_GE(count.load(), 1); // Should have run at least once
    
    // Stop sequence
    executor.stop();
    executor.stop(); // Idempotent call
    
    // Check that no more tasks run after stop
    const int count_after_stop = count.load();
    std::this_thread::sleep_for(200ms);
    BOOST_CHECK_EQUAL(count.load(), count_after_stop);
}

BOOST_AUTO_TEST_SUITE_END() // PeriodicExecutorTests

/** @} */
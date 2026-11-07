#define BOOST_TEST_MODULE PeriodicExecutorTestModule
// Use the header-only version of Boost Test for simpler compilation
#include <boost/test/included/unit_test.hpp>

#include "PeriodicExecutor.hpp" // Include the component under test
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <stdexcept>

using namespace std::chrono_literals;

// Define a safe margin for timer tolerance (e.g., 20% of the period)
// Due to OS scheduling and thread context switching, exact timing is impossible.
constexpr std::chrono::milliseconds TIME_TOLERANCE = 50ms;

/**
 * @brief Polls @p predicate until it returns true or @p deadline expires.
 *
 * @details Returns the predicate's final value. Deadline-polling keeps
 * timing-sensitive assertions robust on slow or loaded CI runners without
 * inflating the common-case test runtime the way fixed sleeps do.
 */
template <typename Pred>
static bool wait_until(Pred predicate,
                       std::chrono::milliseconds deadline = std::chrono::seconds(5)) {
    const auto end = std::chrono::steady_clock::now() + deadline;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end) {
            return predicate();
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

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
    PeriodicExecutor executor;
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
    PeriodicExecutor executor;
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
    PeriodicExecutor executor;
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
    PeriodicExecutor executor;
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

/**
 * @brief Tests that an instance can be restarted after being stopped.
 *
 * @details Verifies the lifecycle fix: after stop() the internal io_context is
 * restarted on the next start(), so the same object schedules work again.
 */
BOOST_AUTO_TEST_CASE(Test_05_RestartAfterStop) {
    PeriodicExecutor executor;
    std::atomic<int> count{0};

    executor.start(100ms, [&]() { count++; });
    std::this_thread::sleep_for(300ms + TIME_TOLERANCE);
    executor.stop();

    const int count_after_first_run = count.load();
    BOOST_CHECK_GE(count_after_first_run, 1);

    // Restart the same instance.
    count.store(0);
    BOOST_CHECK_EQUAL(executor.start(100ms, [&]() { count++; }), true);
    BOOST_CHECK_EQUAL(executor.is_running(), true);
    std::this_thread::sleep_for(300ms + TIME_TOLERANCE);
    executor.stop();

    BOOST_CHECK_GE(count.load(), 1);
    BOOST_TEST_MESSAGE("Test 05 finished. Second-run count: " << count.load());
}

/**
 * @brief Tests that an exception thrown by the callback does not stop the loop.
 *
 * @details Verifies callback isolation: a throwing callback is caught, routed to the
 * installed error handler, and the periodic loop keeps firing.
 */
BOOST_AUTO_TEST_CASE(Test_06_CallbackExceptionIsolation) {
    PeriodicExecutor executor;
    std::atomic<int> count{0};
    std::atomic<int> errors{0};

    executor.set_error_handler([&](std::exception_ptr) { errors++; });

    executor.start(100ms, [&]() {
        count++;
        throw std::runtime_error("boom");
    });

    std::this_thread::sleep_for(350ms + TIME_TOLERANCE);
    executor.stop();

    // The loop must have survived the exceptions and fired multiple times.
    BOOST_CHECK_GE(count.load(), 2);
    BOOST_CHECK_EQUAL(errors.load(), count.load());
    BOOST_TEST_MESSAGE("Test 06 finished. Executions: " << count.load()
                       << ", errors reported: " << errors.load());
}

/**
 * @brief Tests that stop() can be safely called from within the callback.
 *
 * @details Verifies the self-join guard: calling stop() from the worker thread must
 * not deadlock or terminate. The loop must halt after the callback returns, and the
 * destructor must still join the worker cleanly.
 */
BOOST_AUTO_TEST_CASE(Test_07_StopFromCallback) {
    std::atomic<int> count{0};
    {
        PeriodicExecutor executor;
        executor.start(100ms, [&]() {
            if (++count == 2) {
                executor.stop();  // Self-stop from the worker thread.
            }
        });

        // Give the loop time to reach the self-stop and settle.
        std::this_thread::sleep_for(500ms + TIME_TOLERANCE);

        // It should have stopped itself at the 2nd firing and not run again.
        BOOST_CHECK_EQUAL(count.load(), 2);
        BOOST_CHECK_EQUAL(executor.is_running(), false);
        // Destructor runs here and must join the deferred worker without terminating.
    }
    BOOST_CHECK_EQUAL(count.load(), 2);
    BOOST_TEST_MESSAGE("Test 07 finished. Final count: " << count.load());
}

/**
 * @brief Tests that an instance can be restarted after a self-stop.
 *
 * @details A stop() from inside the callback defers the join and leaves the worker
 * handle in place. A subsequent start() from another thread must reap that worker
 * and succeed (this used to call std::terminate). A start() attempted from inside
 * the callback right after the self-stop cannot join the worker from itself and
 * must be refused gracefully (return false) instead of crashing.
 */
BOOST_AUTO_TEST_CASE(Test_08_RestartAfterSelfStop) {
    // Atomics are declared before the executor so the destructor's stop()/join
    // runs while everything the callbacks reference is still alive.
    std::atomic<int> first_run{0};
    std::atomic<int> second_run{0};
    std::atomic<int> restart_inside_result{-1};
    PeriodicExecutor executor;

    BOOST_REQUIRE(executor.start(100ms, [&]() {
        if (++first_run == 2) {
            executor.stop();  // Self-stop from the worker thread; join is deferred.
            // Restart from inside the callback: must be refused, not terminate.
            restart_inside_result.store(
                executor.start(100ms, [&]() { ++second_run; }) ? 1 : 0);
        }
    }));

    // Wait (deadline-polled) until the self-stop has happened. first_run can
    // never exceed 2: after the callback, handle_wait re-checks running_ and
    // does not re-arm, so the exact-equality checks below are safe.
    BOOST_REQUIRE(wait_until([&]() { return restart_inside_result.load() != -1; }));
    BOOST_CHECK_EQUAL(executor.is_running(), false);
    BOOST_CHECK_EQUAL(first_run.load(), 2);
    BOOST_CHECK_EQUAL(restart_inside_result.load(), 0);

    // Restart from this (non-worker) thread: start() must join the stale worker
    // left behind by the self-stop and then succeed.
    BOOST_CHECK_EQUAL(executor.start(100ms, [&]() { ++second_run; }), true);
    BOOST_CHECK_EQUAL(executor.is_running(), true);

    BOOST_CHECK(wait_until([&]() { return second_run.load() >= 1; }));
    executor.stop();

    BOOST_CHECK_GE(second_run.load(), 1);
    BOOST_TEST_MESSAGE("Test 08 finished. Second-run count: " << second_run.load());
}

/**
 * @brief Tests that a restart is not poisoned by handlers queued before stop().
 *
 * @details pause() posts timer_.cancel() onto the strand. When pause() and stop()
 * land while the callback is still executing, that posted cancel never runs in the
 * old generation (io_context::stop() makes run() return mid-queue) and would
 * otherwise execute at the head of the NEXT run(), cancelling the fresh wait and
 * leaving a "running" executor whose callback never fires. start() must drain such
 * stale handlers before re-arming.
 */
BOOST_AUTO_TEST_CASE(Test_09_RestartNotPoisonedByStaleHandlers) {
    std::atomic<int> first_calls{0};
    std::atomic<bool> in_callback{false};
    std::atomic<int> second_calls{0};
    PeriodicExecutor executor;

    BOOST_REQUIRE(executor.start(50ms, [&]() {
        ++first_calls;
        in_callback.store(true);
        std::this_thread::sleep_for(300ms);  // Keep the strand busy for the window.
    }));

    // Enter the window: the callback is running, so pause()'s cancel stays queued.
    BOOST_REQUIRE(wait_until([&]() { return in_callback.load(); }));
    executor.pause();  // Posts timer_.cancel(); it queues behind the busy callback.
    executor.stop();   // Stops the context mid-queue; joins once the callback ends.
    BOOST_CHECK_EQUAL(executor.is_running(), false);

    // Restart: without the drain in start(), the stale cancel would kill the new
    // pending wait and second_calls would stay 0 despite is_running() == true.
    BOOST_REQUIRE(executor.start(50ms, [&]() { ++second_calls; }));
    BOOST_CHECK(wait_until([&]() { return second_calls.load() >= 2; }));
    executor.stop();

    BOOST_CHECK_GE(second_calls.load(), 2);
    BOOST_TEST_MESSAGE("Test 09 finished. Second-generation calls: " << second_calls.load());
}

/**
 * @brief Tests that start() is refused while another thread's stop() is joining.
 *
 * @details stop() joins the worker outside the lock. During that window the old
 * run() may still be executing its final callback; a start() that slipped through
 * would call io_context_.restart() concurrently with the unfinished run() and race
 * on the timer. start() must return false until the join completes, and succeed
 * afterwards.
 */
BOOST_AUTO_TEST_CASE(Test_10_StartRefusedWhileStopJoins) {
    std::atomic<bool> in_callback{false};
    std::atomic<bool> release_callback{false};
    std::atomic<int> second_calls{0};
    PeriodicExecutor executor;

    BOOST_REQUIRE(executor.start(30ms, [&]() {
        in_callback.store(true);
        while (!release_callback.load()) {
            std::this_thread::sleep_for(1ms);  // Pin the callback until released.
        }
    }));

    BOOST_REQUIRE(wait_until([&]() { return in_callback.load(); }));

    // stop() on a second thread blocks in its join until the callback returns.
    std::thread stopper([&]() { executor.stop(); });

    // Once is_running() flips, stop() has published joining_ under the same lock,
    // so any start() acquiring the lock afterwards must refuse while the old
    // worker is still pinned inside its callback.
    BOOST_REQUIRE(wait_until([&]() { return !executor.is_running(); }));
    BOOST_CHECK_EQUAL(executor.start(30ms, [&]() { ++second_calls; }), false);

    release_callback.store(true);  // Let the old generation finish.
    stopper.join();                // stop() completes its join; joining_ clears.

    BOOST_REQUIRE(executor.start(30ms, [&]() { ++second_calls; }));
    BOOST_CHECK(wait_until([&]() { return second_calls.load() >= 1; }));
    executor.stop();

    BOOST_CHECK_GE(second_calls.load(), 1);
    BOOST_TEST_MESSAGE("Test 10 finished. Second-generation calls: " << second_calls.load());
}

BOOST_AUTO_TEST_SUITE_END() // PeriodicExecutorTests

/** @} */
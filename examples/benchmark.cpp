/**
@file benchmark.cpp
@brief Simple benchmark that measures periodic execution jitter and cumulative phase error.
@details
This program uses class PeriodicExecutor
to schedule a recurring callback at a 1 ms interval. The callback records:
instantaneous interval error (absolute difference between actual interval and desired interval),
cumulative phase error (how far the current execution time is from the ideal time computed
from the first execution).
The benchmark runs for 10 seconds and then computes and prints statistics:
average, median, min, max instantaneous jitter (ns and µs),
average (absolute), median, min, max cumulative phase error (ns and µs),
saves raw per-iteration data to timing_data.csv.
@note The code intentionally uses std::chrono::steady_clock for timing to measure intervals and
  avoid issues from system clock adjustments. The value of steady_clock::time_since_epoch()
  has an unspecified epoch and must not be interpreted as system wall-clock time.
  Only elapsed-time differences are meaningful. This file converts time_points to
  integer nanoseconds (steady clock epoch) for easy arithmetic and output.
@warning The benchmark does not perform CPU pinning, priority adjustments, or explicit
     mitigation of OS scheduling variability. Results will vary across systems.
@author Joerg Wallmersperger
@date 2025-10-18
*/
#include "PeriodicExecutor.hpp"
#include <iostream>
#include <chrono>
#include <atomic>
#include <vector>
#include <algorithm> // Required for std::min_element, std::max_element, std::sort
#include <cmath>     // Required for std::abs
#include <fstream>   // Required for file output
/**
@brief Entry point for the benchmark.
@details
Sets up a PeriodicExecutor to invoke a lambda every 1 millisecond. The lambda:
records the current steady_clock time (converted to nanoseconds),
updates a previous-execution timestamp,
computes instantaneous drift (absolute difference between actual interval and desired interval),
accumulates instantaneous drift into a running total,
computes cumulative phase error relative to an ideal schedule anchored at the first execution,
stores a bounded amount of raw timing data for later analysis.
After scheduling, the main thread sleeps for 10 seconds, stops the executor, and computes
statistics from the collected data before printing summary information and saving a CSV.
@returns int exit status (0 on success)
**/
int main() {
/*
@brief Periodic executor instance used to schedule the recurring callback.
@details
This is a template instantiation of the class PeriodicExecutor. The concrete behavior
(threading, timer implementation) is defined in PeriodicExecutor.hpp.
*/
    PeriodicExecutor<> executor;
/**
@var counter
@brief Atomic counter of how many times the callback has been invoked.
@details
Incremented on every callback invocation; used to compute the expected ideal execution
time relative to the first execution. Because it is shared between the callback and the
main thread, it is declared atomic.
*/
    std::atomic<int> counter{0};
/**
@var total_instantaneous_drift_ns
@brief Accumulated sum (ns) of instantaneous drifts used to compute average jitter.
@details
This value sums absolute differences between measured intervals and DESIRED_INTERVAL_NS.
Using an atomic allows safe accumulation from the callback thread without further locking.
*/
    std::atomic<long long> total_instantaneous_drift_ns{0};
/**
@var last_actual_execution_ns
@brief Stores the timestamp (ns) of the previous callback execution.
@details
Atomic to allow the callback to atomically replace it with the current timestamp and
obtain the previous timestamp in a single exchange operation. Initialized to 0 so
the very first execution can be detected and skipped for interval measurements.
Value interpretation: nanoseconds from steady_clock::time_since_epoch() (not wall-clock).
*/
    std::atomic<long long> last_actual_execution_ns{0}; 
/**
@brief Desired interval in nanoseconds between consecutive callback invocations.
@note This benchmark uses 1 ms (1,000,000 ns). All computed drifts are relative to this.
*/
    constexpr long long DESIRED_INTERVAL_NS = 1'000'000LL; // 1 ms
/**
@var timing_data
@brief Stores a bounded vector of per-iteration (instantaneous_drift, cumulative_phase_error).
@details
Used for offline analysis and CSV output. The vector is reserved for up to 10000 entries.
Each element is a pair:
first: instantaneous drift (absolute interval error) in ns,
second: cumulative phase error (signed) in ns.
Access note: the callback pushes to this container without synchronization with the main
thread other than the limited capacity and single-writer assumption. If you migrate this
code to a different executor implementation consider proper synchronization or a lock-free queue.
*/
    std::vector<std::pair<long long, long long>> timing_data;
    timing_data.reserve(10000);
/**
@var first_execution_time_ns
@brief Timestamp (ns) of the very first callback execution (used as schedule anchor).
@details
The benchmark computes an ideal expected execution time for the N-th callback as:
expected = first_execution_time_ns + N * DESIRED_INTERVAL_NS
Because the steady_clock epoch is arbitrary, this is purely a relative anchor.
*/
    std::atomic<long long> first_execution_time_ns{0};
/**
@brief Start the periodic executor with a 1 ms interval and register the measurement callback.
@details
The callback captures local state by reference to update the counters and store timing data.
The callback body is well-commented below: it
gets the current steady_clock time and converts to ns,
sets the first execution time on first call,
swaps the last execution time atomically to obtain the previous timestamp,
skips the very first invocation for interval-based statistics,
computes instantaneous and cumulative errors and stores them.
@note The lambda must be reasonably fast; expensive operations inside the callback will
  affect measured intervals and the quality of the benchmark.
*/
    executor.start(std::chrono::milliseconds(1), [&]() {
        // Capture time point for the current callback invocation.
        auto now = std::chrono::steady_clock::now();
        
        // Convert current time point to nanoseconds since epoch
        long long current_actual_execution_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        // Capture the time of the very first execution
        if (first_execution_time_ns == 0) {
            first_execution_time_ns = current_actual_execution_ns;
        }

        // Exchange current time (current_actual_execution_ns) with the atomic variable, 
        // getting the time of the previous actual execution (prev_actual_execution_ns)
        long long prev_actual_execution_ns = last_actual_execution_ns.exchange(current_actual_execution_ns); 
        
        // Skip the very first execution (when prev_actual_execution_ns is 0)
        if (prev_actual_execution_ns == 0) {
            ++counter;
            return;
        }
        // Increment counter for measured executions
        ++counter;
        // 1. Calculate the actual interval elapsed since the last execution
        long long actual_interval_ns = current_actual_execution_ns - prev_actual_execution_ns;
        // 2. Calculate Instantaneous Drift (Jitter)
        // This is the error of the single interval compared to the desired 1ms
        long long instantaneous_drift = std::abs(actual_interval_ns - DESIRED_INTERVAL_NS);
        // Accumulate total instantaneous drift
        total_instantaneous_drift_ns += instantaneous_drift;
        // Store for detailed analysis
        // 3. Calculate Cumulative Phase Error (Drift from ideal start)
        // This measures how far the current execution is from where it *should* have been
        // if all executions were perfectly on time from the first execution.
        long long expected_execution_ns = first_execution_time_ns + (counter.load() * DESIRED_INTERVAL_NS);
        long long cumulative_phase_error = current_actual_execution_ns - expected_execution_ns;
        if (timing_data.size() < 10000) {
            timing_data.push_back({instantaneous_drift, cumulative_phase_error});
        }
    });
    // Run for 10 seconds
    boost::this_thread::sleep_for(boost::chrono::seconds(10));
    executor.stop();
    // Extract data for statistics
    int measured_executions = counter.load();
    std::vector<long long> instantaneous_drifts_vec;
    std::vector<long long> cumulative_phase_errors_vec;
    for (const auto& data_pair : timing_data) {
        instantaneous_drifts_vec.push_back(data_pair.first);
        cumulative_phase_errors_vec.push_back(data_pair.second);
    }

    // Calculate statistics for instantaneous jitter
    long long avg_instantaneous_drift = measured_executions > 0 ? (total_instantaneous_drift_ns / measured_executions) : 0;
    long long min_instantaneous_drift = instantaneous_drifts_vec.empty() ? 0 : *std::min_element(instantaneous_drifts_vec.begin(), instantaneous_drifts_vec.end());
    long long max_instantaneous_drift = instantaneous_drifts_vec.empty() ? 0 : *std::max_element(instantaneous_drifts_vec.begin(), instantaneous_drifts_vec.end());
    long long median_instantaneous_drift = 0;
    if (!instantaneous_drifts_vec.empty()) {
        std::sort(instantaneous_drifts_vec.begin(), instantaneous_drifts_vec.end());
        median_instantaneous_drift = instantaneous_drifts_vec[instantaneous_drifts_vec.size() / 2];
    }
    
    // Calculate statistics for cumulative phase error
    long long avg_cumulative_phase_error = 0;
    if (measured_executions > 0) {
        long long total_cumulative_error_sum = 0;
        for (long long err : cumulative_phase_errors_vec) {
            total_cumulative_error_sum += std::abs(err);
        }
        avg_cumulative_phase_error = total_cumulative_error_sum / measured_executions;
    }

    long long min_cumulative_phase_error = cumulative_phase_errors_vec.empty() ? 0 : *std::min_element(cumulative_phase_errors_vec.begin(), cumulative_phase_errors_vec.end());
    long long max_cumulative_phase_error = cumulative_phase_errors_vec.empty() ? 0 : *std::max_element(cumulative_phase_errors_vec.begin(), cumulative_phase_errors_vec.end());

    long long median_cumulative_phase_error = 0;
    if (!cumulative_phase_errors_vec.empty()) {
        std::sort(cumulative_phase_errors_vec.begin(), cumulative_phase_errors_vec.end());
        median_cumulative_phase_error = cumulative_phase_errors_vec[cumulative_phase_errors_vec.size() / 2];
    }

    // Print results
    std::cout << "========================================" << std::endl;
    std::cout << "PeriodicExecutor Benchmark Results" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total measured executions: " << measured_executions << std::endl;
    std::cout << "Expected interval: 1 ms (1,000,000 ns)" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "--- Instantaneous Jitter (Interval Error) ---" << std::endl;
    std::cout << "Average Jitter:    " << avg_instantaneous_drift << " ns (" 
              << (avg_instantaneous_drift / 1000.0) << " µs)" << std::endl;
    std::cout << "Median Jitter:     " << median_instantaneous_drift << " ns (" 
              << (median_instantaneous_drift / 1000.0) << " µs)" << std::endl;
    std::cout << "Min Jitter:        " << min_instantaneous_drift << " ns (" 
              << (min_instantaneous_drift / 1000.0) << " µs)" << std::endl;
    std::cout << "Max Jitter:        " << max_instantaneous_drift << " ns (" 
              << (max_instantaneous_drift / 1000.0) << " µs)" << std::endl;
    double instantaneous_drift_percent = (avg_instantaneous_drift / (double)DESIRED_INTERVAL_NS) * 100.0;
    std::cout << "Jitter percentage: " << instantaneous_drift_percent << "%" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "--- Cumulative Phase Error (Drift from Ideal Start) ---" << std::endl;
    std::cout << "Average Cumulative Error (abs): " << avg_cumulative_phase_error << " ns (" 
              << (avg_cumulative_phase_error / 1000.0) << " µs)" << std::endl;
    std::cout << "Median Cumulative Error (signed): " << median_cumulative_phase_error << " ns (" 
              << (median_cumulative_phase_error / 1000.0) << " µs)" << std::endl;
    std::cout << "Min Cumulative Error (signed): " << min_cumulative_phase_error << " ns (" 
              << (min_cumulative_phase_error / 1000.0) << " µs)" << std::endl;
    std::cout << "Max Cumulative Error (signed): " << max_cumulative_phase_error << " ns (" 
              << (max_cumulative_phase_error / 1000.0) << " µs)" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    // Output all timing data to a single CSV file
    try {
        std::ofstream timing_csv_file("timing_data.csv", std::ios::out | std::ios::trunc);
        timing_csv_file.exceptions(std::ofstream::failbit | std::ofstream::badbit);
        timing_csv_file << "InstantaneousJitter,CumulativePhaseError\n";
        for (const auto& entry : timing_data) {
            timing_csv_file << entry.first << "," << entry.second << "\n";
        }
        timing_csv_file.close();
    } catch (const std::ofstream::failure& e) {
        std::cerr << "Failed to write timing_data.csv: " << e.what() << '\n';
        std::cerr << "Falling back to stdout CSV output.\n";
        try {
            std::cout << "InstantaneousJitter,CumulativePhaseError\n";
            for (const auto& entry : timing_data) {
                std::cout << entry.first << "," << entry.second << '\n';
            }
        } catch (...) {
            std::cerr << "Fallback stdout write also failed. Timing data lost.\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error while writing CSV: " << e.what() << '\n';
    }
    return 0;
}

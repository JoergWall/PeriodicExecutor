## Periodic Timer using Boost.Asio for C++ - PeriodicExecutor

The `PeriodicExecutor` is a C++ class that implements an accurate, asynchronous periodic function scheduler using the **Boost.Asio** library. This implementation adheres to best practices by employing the anti-drift pattern and providing explicit control over the timer's lifecycle (Start, Stop, Pause/Resume).

## Features

  * **Anti-Drift Mechanism:** Uses `boost::asio::steady_timer` and relative rescheduling (`timer.expires_at(timer.expiry() + interval)`) to ensure intervals are consistent and prevent cumulative timing errors, regardless of handler execution time.
  * **Asynchronous Operation:** Leverages the `boost::asio::io_context` event loop, ensuring the timer is non-blocking and efficient.
  * **Explicit Control:** Provides public methods (`start()`, `stop()`, and `pause_resume()`) for managing the periodic task externally.
  * **Graceful Shutdown:** Utilizes the timer's `cancel()` function and checks for the `boost::asio::error::operation_aborted` status to ensure clean termination.

## Dependencies

  * **Boost:** Specifically, the **Boost.Asio** library.
  * **C++ Standard Library:** Requires C++11 or later for `std::chrono` and `std::thread`.

## Implementation Details

The core functionality of the `PeriodicExecutor` is based on three Boost.Asio principles:

1.  **`io_context`:** The timer's work is dispatched by the `io_context`. The execution only begins when a thread calls `io_context.run()`.
2.  **State Management:** The internal `is_running_` and `is_paused_` flags, protected by `std::atomic`, are used in conjunction with `timer_.cancel()` to manage the external control operations.
3.  **Control Flow:**
      * **Stop/Pause:** Calling `stop()` or `pause()` issues `timer_.cancel()`. The periodic `handler` detects the resulting `operation_aborted` error code.
      * If `stop()` was called, the handler sees the cancellation and does *not* reschedule, allowing the `io_context::run()` to return.
      * If `pause()` was called, the handler sees the cancellation and checks the `is_paused_` flag, which also prevents rescheduling until `resume` is called.

## Build Instructions
```bash
git clone https://github.com/JoergWall/PeriodicExecutor.git
cd PeriodicExecutor
mkdir build && cd build
cmake ..
make 

```


## Usage Example

The following code demonstrates the class structure and its use with external control threads to simulate real-world application scenarios.

```cpp
#include "PeriodicExecutor.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

/**
 * @file main.cpp
 * @brief Example usage file demonstrating the concurrent execution of periodic tasks.
 *
 * @details This file shows how to instantiate and use the `\`PeriodicExecutor\``
 * class to run three independent tasks (50ms, 200ms, and 1s) concurrently, each
 * managed by its own Boost.Asio context and worker thread.
 */

/**
 * @brief Freestanding function for Task A (50ms).
 *
 * @details A simple function that increments a counter and prints a message.
 * It is wrapped by a lambda in `\`main()\`` to be passed to the executor.
 *
 * @param [in] counter An atomic integer reference, ensuring thread-safe access
 * across different execution threads.
 * @par Returns
 * Nothing.
 */
void func_a(std::atomic<int>& counter) {
    std::cout << "Task A (50ms) executed. Count: " << ++counter << std::endl;
}

/**
 * @brief Freestanding function for Task B (200ms).
 *
 * @details A simple function that increments a counter and prints a message.
 * It is wrapped by a lambda in `\`main()\`` to be passed to the executor.
 *
 * @param [in] counter An atomic integer reference, ensuring thread-safe access
 * across different execution threads.
 * @par Returns
 * Nothing.
 */
void func_b(std::atomic<int>& counter) {
    std::cout << "Task B (200ms) executed. Count: " << ++counter << std::endl;
}

/**
 * @fn main
 * @brief Main function demonstrating concurrent periodic execution.
 *
 * @details This program initializes three `\`PeriodicExecutor\`` instances: `\`executor_a\``,
 * `\`executor_b\``, and `\`executor_c\``, each running at a different frequency.
 * Boost.Asio (via `\`PeriodicExecutor\``) is essential here as it provides the
 * non-blocking asynchronous timing loop and thread isolation, allowing all three
 * tasks to run concurrently without blocking the main application thread.
 * @return `0` on successful execution.
 */
int main() {
    // Create three separate PeriodicExecutor instances. Since the class is templated
    // with a default type, we use the simple syntax `PeriodicExecutor<>`.
    PeriodicExecutor<> executor_a;
    PeriodicExecutor<> executor_b;
    PeriodicExecutor<> executor_c;
    
    // Use atomic counters (`std::atomic`) for thread-safe counting, as each
    // executor will run its task on a different background thread.
    std::atomic<int> count_a{0};
    std::atomic<int> count_b{0};
    std::atomic<int> count_c{0};

    std::cout << "Starting the periodic executors..." << std::endl;

    // Task A (50ms): Using a lambda function to capture the counter by reference
    // and call the freestanding function `func_a`.
    executor_a.start(std::chrono::milliseconds(50), [&]() { func_a(count_a); });

    // Task B (200ms): Using a lambda function to wrap `func_b`.
    executor_b.start(std::chrono::milliseconds(200), [&]() { func_b(count_b); });

    // Task C (1s): Using a direct lambda function definition for the callback.
    executor_c.start(std::chrono::seconds(1), [&]() { 
        std::cout << "Task C (1s) executed. Count: " << ++count_c << std::endl; 
    });

    std::cout << "All executors started on separate threads. They will run for 10 seconds." << std::endl;

    // Pause one of the executors after 5 seconds to demonstrate `pause()`/`resume()`.
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::cout << "\n--- PAUSING Task B for 2 seconds ---\n" << std::endl;
    executor_b.pause();

    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "\n--- RESUMING Task B ---\n" << std::endl;
    executor_b.resume();

    // Block the main thread for the remaining time to allow the worker threads to run.
    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::cout << "\nStopping the periodic executors..." << std::endl;

    // Gracefully stop all three executors. The `stop()` function ensures safe
    // termination and resource cleanup via `thread::join()`.
    executor_a.stop();
    executor_b.stop();
    executor_c.stop();

    std::cout << "Executors stopped." << std::endl;
    std::cout << "Final count for Task A (50ms): " << count_a << std::endl;
    std::cout << "Final count for Task B (200ms): " << count_b << std::endl;
    std::cout << "Final count for Task C (1s): " << count_c << std::endl;

    return 0;
}
```
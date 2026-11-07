# PeriodicExecutor - Periodic Timer using Boost.Asio for C++

![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Boost](https://img.shields.io/badge/Boost-1.70%2B-orange.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)

The `PeriodicExecutor` is a C++ include-only class that implements an accurate, asynchronous periodic function scheduler using the **Boost.Asio** library. This implementation adheres to best practices by employing the anti-drift pattern and providing explicit, thread-safe control over the timer's lifecycle (start, stop, pause, resume). Each instance is self-contained: it owns a private `io_context` driven by a single dedicated worker thread, so scheduling a task never blocks the caller.

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Implementation Details](#implementation-details)
- [Build Instructions](#build-instructions)
- [Usage Example](#usage-example)
- [License](#license)

## Features

- **Anti-Drift Mechanism:** Uses `boost::asio::steady_timer` and relative rescheduling (`timer.expires_at(timer.expiry() + interval)`) to keep intervals consistent and prevent cumulative timing errors, regardless of handler execution time. If a callback overruns its interval, the schedule is re-anchored to *now* to avoid a burst of catch-up firings.
- **Asynchronous Operation:** Leverages the `boost::asio::io_context` event loop, so the timer is non-blocking and efficient.
- **Thread-Safe Control:** `start()`, `stop()`, `pause()`, and `resume()` may be called from any thread. Lifecycle transitions are serialized with a mutex; all timer mutations are marshalled onto a `strand` so the `steady_timer` is only ever touched from the worker thread.
- **Callback Isolation:** An exception escaping the user callback is caught, forwarded to an optional error handler, and the periodic loop keeps running instead of silently dying.
- **Restartable:** After `stop()`, the same instance can be started again — including after a `stop()` issued from inside the callback (the next `start()` reaps the worker thread left behind by such a self-stop). On restart, handlers left queued by the previous generation are discarded before the timer is re-armed.
- **Graceful Shutdown:** Releases the work guard, stops the `io_context`, and joins the worker thread for deterministic cleanup (also invoked automatically by the destructor).

## Requirements

- **Boost:** Specifically, the **Boost.Asio** library (header-only; no separate Boost link is required by the header itself).
- **C++ Standard Library:** Requires **C++17** or later (uses `std::optional`, `std::chrono`, `std::thread`, `std::atomic`).

## API

| Method | Description |
| :--- | :--- |
| `bool start(std::chrono::milliseconds interval, std::function<void()> callback)` | Starts the loop; first call fires one interval later. Returns `false` if already running, the interval is non-positive, the callback is empty, the call is made from inside the callback after a self-stop (the worker cannot join itself), or another thread is concurrently stopping/restarting the executor. |
| `void stop()` | Stops and joins the worker thread. Idempotent. Safe to call from inside the callback — the join is deferred (see below) rather than deadlocking. |
| `void pause()` / `void resume()` | Suspend / restart the loop without tearing down the worker thread. Idempotent. |
| `void set_error_handler(std::function<void(std::exception_ptr)>)` | Installs a handler for callback/run-loop exceptions. Set it before `start()`. |
| `bool is_running() const` / `bool is_paused() const` | Observe the current state. |

## Implementation Details

The core functionality of the `PeriodicExecutor` is based on a few Boost.Asio principles:

1. **`io_context`:** The timer's work is dispatched by the internal `io_context`. Execution runs on a dedicated worker thread that calls `io_context::run()`. A `work_guard` keeps `run()` alive across pauses; `start()` calls `io_context::restart()` and drains any handlers the previous generation left queued, so the instance can be reused safely after `stop()`.
2. **State Management:** The observable `running_` and `paused_` flags are `std::atomic<bool>`. A `std::mutex` serializes the lifecycle methods against each other, and a `strand` serializes every access to the timer.
3. **Control Flow:**
      - **Stop:** `stop()` releases the work guard and calls `io_context::stop()`, then joins the worker thread. The join happens outside the lock so a concurrent control call cannot deadlock. If `stop()` is called from *within* the callback (i.e. on the worker thread itself), the self-join is detected and skipped — shutdown proceeds and the join is deferred to the destructor, a later `stop()`, or the next `start()` from another thread, so the loop halts once the current callback returns without deadlocking or terminating. While a `stop()` is joining the worker, a concurrent `start()` returns `false` instead of restarting over the unfinished `run()`.
      - **Pause:** `pause()` sets `paused_` and `post`s `timer_.cancel()` onto the strand. The handler sees `operation_aborted` (or the `paused_` flag) and does *not* reschedule.
      - **Resume:** `resume()` clears `paused_` and `post`s a fresh `async_wait` onto the strand, re-arming the loop one interval from now.

## Build Instructions

```bash
git clone https://github.com/JoergWall/PeriodicExecutor.git
cd PeriodicExecutor
mkdir build && cd build
cmake ..
make 

```

## Usage Example

The following code demonstrates the periodic execution of functions in different timing intervals:

```cpp
#include "PeriodicExecutor.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

/**
 * @file examples/example_PE01.cpp
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
    // Create three separate PeriodicExecutor instances. The class is self-contained;
    // each instance owns its own worker thread and Boost.Asio io_context.
    PeriodicExecutor executor_a;
    PeriodicExecutor executor_b;
    PeriodicExecutor executor_c;
    
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

## License

MIT License, Copyright (c) 2025 Joerg Wallmersperger, see the [LICENSE](LICENSE) for full license details.

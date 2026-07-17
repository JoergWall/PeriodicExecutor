#ifndef PERIODIC_EXECUTOR_HPP
#define PERIODIC_EXECUTOR_HPP

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

/**
 * @file
 * @brief Header file for the PeriodicExecutor class.
 */

/**
 * @class PeriodicExecutor
 * @brief A thread-safe, self-contained service for accurate periodic task execution.
 *
 * @details `PeriodicExecutor` encapsulates the complexity of thread management and
 * asynchronous timing using Boost.Asio. It owns a private `io_context` driven by a
 * single dedicated worker thread, so a task scheduled on one instance never blocks
 * the caller's thread or any other instance.
 *
 * Design guarantees:
 * - **Anti-drift:** the next expiry is computed relative to the *previous* scheduled
 *   expiry (`expires_at(expiry() + interval)`), so handler latency does not accumulate.
 *   If a callback runs longer than one interval, the schedule is re-anchored to "now"
 *   to avoid a burst of catch-up firings.
 * - **Thread safety:** all timer mutations happen on a `strand` bound to the worker
 *   thread; control methods that originate on other threads (`pause`, `resume`) `post`
 *   their timer work onto that strand. Lifecycle transitions are serialized with a
 *   mutex, and the observable state flags are `std::atomic`.
 * - **Callback isolation:** an exception escaping the user callback is caught, reported
 *   through the optional error handler, and the periodic loop keeps running.
 * - **Restartable:** after `stop()`, the same instance may be `start()`ed again.
 *
 * The class is neither copyable nor movable; it owns a running thread and an
 * `io_context`, which are not safely relocatable.
 */
class PeriodicExecutor {
public:
    /** @brief Signature of the optional handler invoked when the worker thread or a
     *  user callback throws. Receives the captured exception. Must not throw. */
    using ErrorHandler = std::function<void(std::exception_ptr)>;

    /**
     * @brief Constructs an idle executor. No thread is started until start() is called.
     */
    PeriodicExecutor();

    /**
     * @brief Destructor. Calls stop() to join the worker thread and release resources.
     */
    ~PeriodicExecutor();

    PeriodicExecutor(const PeriodicExecutor&) = delete;
    PeriodicExecutor& operator=(const PeriodicExecutor&) = delete;
    PeriodicExecutor(PeriodicExecutor&&) = delete;
    PeriodicExecutor& operator=(PeriodicExecutor&&) = delete;

    /**
     * @brief Installs a handler for exceptions thrown by the callback or the run loop.
     * @details Set this before calling start(). Changing it while running is a data
     * race. If no handler is installed, exceptions are swallowed and the loop continues.
     * @param[in] handler Callable taking a std::exception_ptr; must not throw.
     */
    void set_error_handler(ErrorHandler handler);

    /**
     * @brief Starts periodic execution of @p callback every @p interval.
     * @details The first invocation occurs one interval after the call. Spawns the
     * worker thread that drives the internal io_context.
     * @param[in] interval Time between executions. Must be strictly positive.
     * @param[in] callback The task to run periodically. Must be non-empty.
     * @return `true` if started; `false` if already running, or if @p interval is
     *         non-positive, or if @p callback is empty.
     */
    bool start(std::chrono::milliseconds interval, std::function<void()> callback);

    /**
     * @brief Stops execution and joins the worker thread. Idempotent.
     * @details Releases the work guard, stops the io_context, and joins the worker.
     *
     * Safe to call from within the callback: a thread cannot join itself, so in that
     * case the shutdown is performed but the join is deferred to the destructor or to a
     * later stop() from another thread. `stop()` therefore does not block when called
     * from the callback, and the loop halts as soon as the current callback returns.
     */
    void stop();

    /**
     * @brief Suspends execution without tearing down the worker thread. Idempotent.
     * @details Cancels the pending wait (on the strand) and sets the paused flag so the
     * handler does not reschedule. No effect if not running or already paused.
     */
    void pause();

    /**
     * @brief Resumes execution after pause(). Idempotent.
     * @details Re-arms the timer one interval from now. No effect if not running or not paused.
     */
    void resume();

    /** @brief Returns true between a successful start() and the next stop(). */
    bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

    /** @brief Returns true while paused. */
    bool is_paused() const noexcept { return paused_.load(std::memory_order_acquire); }

private:
    /**
     * @brief Timer completion handler. Runs on the strand (worker thread).
     * @details Returns without rescheduling on cancellation, pause, or stop. Otherwise
     * runs the callback (exceptions contained), then re-arms using the anti-drift rule.
     */
    void handle_wait(const boost::system::error_code& error);

    /// Starts an async_wait whose handler is dispatched through the strand.
    void arm_timer();

    /// Forwards a captured exception to the error handler, if one is installed.
    void report_error(std::exception_ptr eptr) noexcept;

    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    boost::asio::io_context io_context_;                       ///< Owns the task queue.
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;  ///< Serializes handlers.
    boost::asio::steady_timer timer_;                          ///< Monotonic scheduling timer.
    std::optional<WorkGuard> work_guard_;                      ///< Keeps run() alive; reset by stop().
    std::thread worker_thread_;                                ///< Drives io_context_.run().

    std::function<void()> callback_;                           ///< User task.
    ErrorHandler error_handler_;                               ///< Optional exception sink.
    std::chrono::milliseconds interval_{0};                    ///< Period between executions.

    std::mutex control_mutex_;                                 ///< Serializes lifecycle transitions.
    std::atomic<bool> running_{false};                         ///< Observable "started" state.
    std::atomic<bool> paused_{false};                          ///< Observable "paused" state.
};

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

inline PeriodicExecutor::PeriodicExecutor()
    : strand_(boost::asio::make_strand(io_context_)),
      timer_(io_context_) {}

inline PeriodicExecutor::~PeriodicExecutor() {
    stop();
}

inline void PeriodicExecutor::set_error_handler(ErrorHandler handler) {
    std::lock_guard<std::mutex> lock(control_mutex_);
    error_handler_ = std::move(handler);
}

inline void PeriodicExecutor::report_error(std::exception_ptr eptr) noexcept {
    // error_handler_ is treated as immutable while running (see set_error_handler docs),
    // so it is read here without locking to avoid deadlocking against stop()'s join().
    if (error_handler_) {
        try {
            error_handler_(eptr);
        } catch (...) {
            // A throwing error handler must never take down the worker thread.
        }
    }
}

inline void PeriodicExecutor::arm_timer() {
    timer_.async_wait(boost::asio::bind_executor(
        strand_, [this](const boost::system::error_code& error) { handle_wait(error); }));
}

inline bool PeriodicExecutor::start(std::chrono::milliseconds interval,
                                    std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(control_mutex_);
    if (running_.load(std::memory_order_relaxed) || interval.count() <= 0 || !callback) {
        return false;
    }

    callback_ = std::move(callback);
    interval_ = interval;
    paused_.store(false, std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);

    // Prepare the context for a (possibly repeated) run and keep it alive.
    io_context_.restart();
    work_guard_.emplace(io_context_.get_executor());

    timer_.expires_after(interval_);
    arm_timer();

    worker_thread_ = std::thread([this] {
        try {
            io_context_.run();
        } catch (...) {
            report_error(std::current_exception());
        }
    });

    return true;
}

inline void PeriodicExecutor::stop() {
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(control_mutex_);

        // Signal shutdown (only meaningful the first time, but harmless to repeat).
        if (running_.load(std::memory_order_relaxed)) {
            running_.store(false, std::memory_order_release);
            paused_.store(false, std::memory_order_relaxed);
            work_guard_.reset();  // Allow run() to return once outstanding work drains.
            io_context_.stop();   // Return from run() promptly, even mid-queue.
        }

        // A thread cannot join itself. If stop() was called from within the callback,
        // leave the handle in place: the destructor or a later stop() from another
        // thread will join it. The io_context is already stopped, so the worker exits
        // as soon as the current callback returns.
        if (worker_thread_.get_id() == std::this_thread::get_id()) {
            return;
        }
        worker = std::move(worker_thread_);
    }
    // Join outside the lock so a control call from another thread cannot deadlock,
    // and so the mutex is free while we block.
    if (worker.joinable()) {
        worker.join();
    }
}

inline void PeriodicExecutor::pause() {
    std::lock_guard<std::mutex> lock(control_mutex_);
    if (!running_.load(std::memory_order_relaxed) || paused_.load(std::memory_order_relaxed)) {
        return;
    }
    paused_.store(true, std::memory_order_release);
    // Cancel on the strand so the timer is only ever touched from the worker thread.
    boost::asio::post(strand_, [this] { timer_.cancel(); });
}

inline void PeriodicExecutor::resume() {
    std::lock_guard<std::mutex> lock(control_mutex_);
    if (!running_.load(std::memory_order_relaxed) || !paused_.load(std::memory_order_relaxed)) {
        return;
    }
    paused_.store(false, std::memory_order_release);
    boost::asio::post(strand_, [this] {
        timer_.expires_after(interval_);
        arm_timer();
    });
}

inline void PeriodicExecutor::handle_wait(const boost::system::error_code& error) {
    if (error == boost::asio::error::operation_aborted ||
        paused_.load(std::memory_order_acquire) ||
        !running_.load(std::memory_order_acquire)) {
        return;
    }

    try {
        callback_();
    } catch (...) {
        report_error(std::current_exception());
    }

    // The callback may have stopped or paused the executor; if so, do not re-arm.
    if (!running_.load(std::memory_order_acquire) ||
        paused_.load(std::memory_order_acquire)) {
        return;
    }

    // Anti-drift: schedule relative to the previous expiry. If the callback overran
    // the interval, re-anchor to now so we don't fire a burst to "catch up".
    auto next = timer_.expiry() + interval_;
    const auto now = std::chrono::steady_clock::now();
    if (next < now) {
        next = now + interval_;
    }
    timer_.expires_at(next);
    arm_timer();
}

#endif  // PERIODIC_EXECUTOR_HPP

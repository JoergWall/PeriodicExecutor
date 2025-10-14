#ifndef PERIODIC_EXECUTOR_HPP
#define PERIODIC_EXECUTOR_HPP
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <functional>
#include <chrono>

/**
 * @file
 * @brief Header file for the PeriodicExecutor class template.
 */

/**
 * @class PeriodicExecutor
 * @tparam Executor The type of the Boost.Asio executor to use for scheduling.
 * @brief Provides a robust, thread-safe service for periodic task execution.
 *
 * @details The `PeriodicExecutor` encapsulates the complexity of thread management
 * and asynchronous timing using the Boost.Asio library. Boost.Asio provides the
 * core mechanism (`\`io_context\`` and `\`steady_timer\``) to schedule tasks
 * non-blockingly, making it suitable for high-performance concurrent applications.
 * The class uses a dedicated worker thread, a `\`work_guard\`` for lifecycle
 * management, and a `\`strand\`` to guarantee that the user-provided callback
 * is always executed serially, preventing potential data races.
 */
template <typename Executor = boost::asio::io_context::executor_type>
class PeriodicExecutor {
public:
    /**
     * @brief Constructs a new `PeriodicExecutor` instance using the internal executor.
     * @details This is the default constructor. It initializes the `\`strand_\`` using the
     * executor provided by the internal `\`io_context_\``.
     */
    PeriodicExecutor();

    /**
     * @brief Constructs a new `PeriodicExecutor` instance using an external executor.
     * @param[in] executor The Boost.Asio executor to use for scheduling tasks. This
     * allows the periodic task to be executed on an external thread pool or context.
     */
    explicit PeriodicExecutor(Executor executor);

    /**
     * @brief Destructor for `PeriodicExecutor`.
     * @details Calls `\`stop()\`` to ensure the worker thread and all associated resources
     * are properly cleaned up before the object is destroyed.
     */
    ~PeriodicExecutor();

    /**
     * @brief Starts the periodic execution of the task.
     *
     * @details This function initiates the worker thread and starts the first asynchronous wait
     * for the periodic timer. This function can only be called once.
     *
     * @param[in] interval The time interval (duration) between task executions.
     * @param[in] callback The function (e.g., `\`std::function\``, lambda, or functor)
     * to be executed periodically.
     * @return A boolean indicating if the executor was started successfully (`true`)
     * or was already running (`false`).
     */
    bool start(std::chrono::milliseconds interval, std::function<void()> callback);

    /**
     * @brief Stops the periodic execution and safely joins the worker thread.
     *
     * @details This function performs a coordinated, graceful shutdown: it cancels the
     * pending asynchronous wait, signals the `\`io_context\`` to stop processing tasks
     * (by resetting the `\`work_guard\``), and then blocks until the worker thread
     * completes its execution using `\`thread::join()\``.
     * Can be called multiple times.
     */
    void stop();

    /**
     * @brief Pauses the periodic execution.
     *
     * @details This function immediately cancels the pending timer, which prevents the next
     * scheduled execution of the task. The underlying worker thread remains alive
     * and the `\`io_context\`` stays active.
     * This function has no effect if the executor is not running or is already paused.
     */
    void pause();

    /**
     * @brief Resumes the periodic execution.
     *
     * @details This function re-arms the `\`steady_timer\`` to expire after the defined
     * interval, effectively restarting the periodic loop. The task will be executed
     * again at the specified frequency.
     * This function has no effect if the executor is not running or is not paused.
     */
    void resume();

    // Prevent copying and copy-assignment to maintain control over the single
    // worker thread and `io_context` instance.
    PeriodicExecutor(const PeriodicExecutor&) = delete;
    PeriodicExecutor& operator=(const PeriodicExecutor&) = delete;

private:
    /**
     * @brief The core handler function for the `\`async_wait\`` timer operation.
     *
     * @details This private member function is invoked by the `\`io_context\`` when the
     * `\`steady_timer\`` expires. It is responsible for three critical steps:
     * 1. Checking the `\`error\`` code for cancellation (`\`operation_aborted\``).
     * 2. Executing the user's `\`callback_\`` (guaranteed to be sequential by the `\`strand_\``).
     * 3. Re-arming the timer for the next execution cycle using `\`expires_at()\`` to prevent drift.
     *
     * @param[in] error The error code associated with the asynchronous operation. If non-zero,
     * it indicates cancellation or failure.
     */
    void handle_wait(const boost::system::error_code& error);

    std::function<void()> callback_;
    /**< @brief The user-supplied periodic task, stored as a generic `\`std::function\``. */
    boost::asio::io_context io_context_;
    /**< @brief Boost.Asio's execution context, managing the task queue. */
    boost::asio::steady_timer timer_;
    /**< @brief The monotonic timer used to schedule periodic calls. */
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
    /**< @brief Prevents `\`io_context::run()\`` from exiting when there are no pending tasks. */
    boost::asio::strand<Executor> strand_;
    /**< @brief Guarantees serial execution of handlers, ensuring thread safety for the callback. */
    boost::thread worker_thread_;
    /**< @brief The dedicated thread that calls `\`io_context::run()\`` to execute tasks. */
    std::chrono::milliseconds interval_;
    /**< @brief The desired period between task executions. */
    bool is_running_ = false;
    /**< @brief State flag indicating if the worker thread is active. */
    bool is_paused_ = false;
    /**< @brief State flag indicating if the timer loop is currently suspended. */
};

// PeriodicExecutor Implementation 


/**
 * @fn PeriodicExecutor::PeriodicExecutor()
 * @brief Constructs a new `PeriodicExecutor` instance using the internal executor.
 *
 * @details Initializes the `\`steady_timer\``, creates the `\`work_guard\`` to keep the
 * `\`io_context\`` alive, and initializes the `\`strand\`` using the internal
 * `\`io_context::get_executor()\`` to ensure sequential execution.
 */
template <typename Executor>
PeriodicExecutor<Executor>::PeriodicExecutor()
    : timer_(io_context_),
      work_guard_(boost::asio::make_work_guard(io_context_)),
      strand_(io_context_.get_executor()) {}

/**
 * @fn PeriodicExecutor::PeriodicExecutor(Executor executor)
 * @brief Constructs a new `PeriodicExecutor` instance using an external executor.
 *
 * @details This constructor allows dependency injection of an `Executor`, enabling
 * the periodic task to integrate into an existing, potentially multi-threaded,
 * Boost.Asio execution context. The `\`strand\`` is initialized with this external
 * `\`Executor\`` to maintain the guarantee of serial execution of the task.
 * @param[in] executor The external Boost.Asio executor.
 */
template <typename Executor>
PeriodicExecutor<Executor>::PeriodicExecutor(Executor executor)
    : timer_(io_context_),
      work_guard_(boost::asio::make_work_guard(io_context_)),
      strand_(executor) {}

/**
 * @fn PeriodicExecutor::~PeriodicExecutor()
 * @brief Destructor for `PeriodicExecutor`.
 *
 * @details Ensures the worker thread is safely terminated by calling `\`stop()\``
 * before the object is destroyed. This is crucial for avoiding resource leaks
 * and undefined behavior from detached threads.
 */
template <typename Executor>
PeriodicExecutor<Executor>::~PeriodicExecutor() {
    stop();
}

/**
 * @fn PeriodicExecutor::start(std::chrono::milliseconds interval, std::function<void()> callback)
 * @brief Starts the periodic execution.
 *
 * @details Initializes state variables, sets the timer's initial expiry time, and
 * launches the asynchronous loop by calling `\`async_wait()\``. The use of
 * `\`boost::asio::bind_executor(strand_,...)\`` guarantees the handler will execute
 * through the `\`strand\`` for thread safety. A `\`boost::thread\`` is then launched
 * to call `\`io_context::run()\``, isolating the task execution from the calling thread.
 * @param[in] interval The time interval (duration) between task executions.
 * @param[in] callback The periodic function to execute.
 * @return `true` if the executor started; `false` if it was already running.
 */
template <typename Executor>
bool PeriodicExecutor<Executor>::start(std::chrono::milliseconds interval, std::function<void()> callback) {
    if (is_running_) {
        return false;
    }

    callback_ = callback;
    interval_ = interval;
    is_running_ = true;
    is_paused_ = false;

    timer_.expires_from_now(interval_);
    // Use bind_executor with the strand to ensure the handler is run serially.
    timer_.async_wait(boost::asio::bind_executor(strand_, std::bind(&PeriodicExecutor::handle_wait, this, std::placeholders::_1)));

    // Launch a new thread to run the io_context.
    // The lambda function captures 'this' to correctly call io_context_.run().
    worker_thread_ = boost::thread([this]() {
        io_context_.run();
    });

    return true;
}

/**
 * @fn PeriodicExecutor::stop()
 * @brief Stops the periodic execution and safely joins the worker thread.
 *
 * @details This four-step shutdown process is essential for correct Boost.Asio
 * resource management:
 * 1. `\`timer_.cancel()\``: Cancels the pending wait, causing `\`handle_wait\`` to be called with `\`operation_aborted\``.
 * 2. `\`work_guard_.reset()\``: Releases the "work" object, allowing `\`io_context::run()\`` to eventually exit.
 * 3. `\`io_context_.stop()\``: Signals the context to cease all activity immediately.
 * 4. `\`worker_thread_.join()\``: Blocks until the worker thread has safely terminated, preventing a dangling thread.
 */
template <typename Executor>
void PeriodicExecutor<Executor>::stop() {
    if (!is_running_) {
        return;
    }

    boost::system::error_code ec;
    timer_.cancel(ec);

    work_guard_.reset();
    io_context_.stop();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    is_running_ = false;
    is_paused_ = false;
}

/**
 * @fn PeriodicExecutor::pause()
 * @brief Pauses the periodic execution.
 *
 * @details Achieves pause functionality by cancelling the pending `\`async_wait\``
 * operation on the timer. The `\`handle_wait\`` function is designed to check the
 * `\`is_paused_\`` flag and the cancellation error, preventing the timer from
 * being re-armed until `\`resume()\`` is called.
 */
template <typename Executor>
void PeriodicExecutor<Executor>::pause() {
    if (!is_running_ || is_paused_) {
        return;
    }
    boost::system::error_code ec;
    timer_.cancel(ec);
    is_paused_ = true;
}

/**
 * @fn PeriodicExecutor::resume()
 * @brief Resumes the periodic execution.
 *
 * @details Restarts the periodic loop by setting the `\`is_paused_\`` flag to `false`
 * and initiating a new `\`async_wait\`` operation. The new wait is scheduled immediately
 * relative to the current time.
 */
template <typename Executor>
void PeriodicExecutor<Executor>::resume() {
    if (!is_running_ ||!is_paused_) {
        return;
    }
    is_paused_ = false;
    timer_.expires_from_now(interval_);
    timer_.async_wait(boost::asio::bind_executor(strand_, std::bind(&PeriodicExecutor::handle_wait, this, std::placeholders::_1)));
}

/**
 * @fn PeriodicExecutor::handle_wait(const boost::system::error_code& error)
 * @brief The core timer handler logic.
 *
 * @details This function implements the repeating timer pattern. If the error code
 * is `\`operation_aborted\`` (due to `\`stop()\`` or `\`pause()\``) or if the
 * system is explicitly paused, the function returns without re-arming the timer.
 * Otherwise, it executes the callback and sets the next expiry time relative to
 * the *previous* expiry time (`\`timer_.expiry()\``) to prevent timing drift.
 * @param[in] error The error code from the Boost.Asio asynchronous operation.
 */
template <typename Executor>
void PeriodicExecutor<Executor>::handle_wait(const boost::system::error_code& error) {
    // If the timer was canceled or the executor is paused, exit gracefully.
    if (error == boost::asio::error::operation_aborted || is_paused_) {
        return;
    }

    // Execute the user callback. The strand guarantees this is serialized (one thread at a time).
    callback_();

    // Re-arm the timer for the next interval.
    // Use timer_.expiry() to calculate the next expiry time relative to the old one,
    // thereby maintaining the phase and avoiding clock drift.
    timer_.expires_at(timer_.expiry() + interval_);
    timer_.async_wait(boost::asio::bind_executor(strand_, std::bind(&PeriodicExecutor::handle_wait, this, std::placeholders::_1)));
}

#endif // PERIODIC_EXECUTOR_HPP
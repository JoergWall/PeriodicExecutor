#include "PeriodicExecutor.hpp"
#include <iostream>
#include <chrono>
#include <atomic>

int main() {
    PeriodicExecutor<> executor;
    std::atomic<int> counter{0};
    std::atomic<long long> total_drift_ns{0};
    
    auto start = std::chrono::steady_clock::now();
    
    executor.start(std::chrono::milliseconds(1), [&]() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - start).count();
        
        long long expected_ns = counter.load() * 1'000'000LL;
        long long drift = std::abs(elapsed - expected_ns);
        total_drift_ns += drift;
        
        ++counter;
    });
    
    boost::this_thread::sleep_for(boost::chrono::seconds(10));
    executor.stop();
    
    std::cout << "Total executions: " << counter << std::endl;
    std::cout << "Average drift: " << (total_drift_ns / counter) << " ns" << std::endl;
    
    return 0;
}
// solutions/SpinlockYield.hpp
//
// A diagnostic variant: busy-wait that calls std::this_thread::yield() each
// iteration. Unlike the pause hint, yield asks the OS scheduler to run another
// runnable thread — so the waiting thread *does* give up the core. This keeps
// the lock-free fast path but removes most of the CPU-starvation penalty under
// oversubscription, at the cost of repeated scheduler calls.
//
// It sits between the naked Spinlock and the condition_variable: no kernel
// parking, but cooperative descheduling. Comparing it against both ends pins
// down why the condition variable wins. See ../README.md.

#pragma once

#include <atomic>
#include <functional>
#include <thread>

namespace pio {

class FooSpinYield {
public:
    FooSpinYield() = default;

    void first(std::function<void()> printFirst) {
        printFirst();
        stage_.store(1, std::memory_order_release);
    }

    void second(std::function<void()> printSecond) {
        while (stage_.load(std::memory_order_acquire) != 1) {
            std::this_thread::yield();
        }
        printSecond();
        stage_.store(2, std::memory_order_release);
    }

    void third(std::function<void()> printThird) {
        while (stage_.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        printThird();
    }

private:
    std::atomic<int> stage_{0};
};

} // namespace pio

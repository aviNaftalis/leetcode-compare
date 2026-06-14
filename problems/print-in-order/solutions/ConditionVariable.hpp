// solutions/ConditionVariable.hpp
//
// The accepted submission: a mutex + condition variable. A thread that arrives
// early blocks on cv.wait() — the kernel parks it (a futex wait) and gives the
// CPU back to whoever can make progress. No cycles are burned while waiting.
//
// Cost: larger object (mutex + condition_variable + state) and a syscall on the
// slow path. Benefit: zero CPU contention while waiting, which is what makes it
// win under oversubscription. See ../README.md.

#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>

namespace pio {

class FooCV {
public:
    FooCV() = default;

    void first(std::function<void()> printFirst) {
        {
            std::lock_guard<std::mutex> lk(m_);
            printFirst();
            stage_ = 1;
        }
        cv_.notify_all();
    }

    void second(std::function<void()> printSecond) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this] { return stage_ == 1; });
        printSecond();
        stage_ = 2;
        lk.unlock();
        cv_.notify_all();
    }

    void third(std::function<void()> printThird) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this] { return stage_ == 2; });
        printThird();
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    int stage_ = 0; // 0 = none, 1 = first done, 2 = second done
};

} // namespace pio

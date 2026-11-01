#pragma once

// Single-producer / single-consumer lock-free ring. Real-time safe on both
// ends: no allocation after construction, no locks.

#include <atomic>
#include <cstddef>
#include <vector>

namespace sg {

template <class T>
class Spsc {
public:
    explicit Spsc(std::size_t capacity) : buf_(capacity + 1) {}

    bool push(const T& v) noexcept {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t next = (h + 1) % buf_.size();
        if (next == tail_.load(std::memory_order_acquire)) return false;  // full
        buf_[h] = v;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& out) noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;  // empty
        out = buf_[t];
        tail_.store((t + 1) % buf_.size(), std::memory_order_release);
        return true;
    }

private:
    std::vector<T> buf_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace sg

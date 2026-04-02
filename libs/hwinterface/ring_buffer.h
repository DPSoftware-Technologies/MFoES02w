#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <memory>

namespace southbridge {

template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : capacity_(capacity)
        , mask_(capacity - 1)
        , buf_(std::make_unique<T[]>(capacity))
        , head_(0)
        , tail_(0)
    {
        assert((capacity & (capacity - 1)) == 0 && "capacity must be power of two");
    }

    /* producer: try to push one item.  Returns false if full. */
    bool push(const T& item) {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t next = (h + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire))
            return false;   /* full */
        buf_[h] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    /* producer: overwrite oldest if full (drop-oldest policy) */
    void push_overwrite(const T& item) {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t next = (h + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) {
            /* advance tail (drop oldest) */
            const size_t t = tail_.load(std::memory_order_relaxed);
            tail_.store((t + 1) & mask_, std::memory_order_release);
        }
        buf_[h] = item;
        head_.store(next, std::memory_order_release);
    }

    /* consumer: try to pop one item.  Returns false if empty. */
    bool pop(T& out) {
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire))
            return false;   /* empty */
        out = buf_[t];
        tail_.store((t + 1) & mask_, std::memory_order_release);
        return true;
    }

    /* number of items currently in the ring */
    size_t size() const {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (h - t) & mask_;
    }

    size_t capacity() const { return capacity_; }
    bool   empty()    const { return size() == 0; }

    double fill_pct() const {
        return static_cast<double>(size()) / static_cast<double>(capacity_ - 1) * 100.0;
    }

private:
    const size_t             capacity_;
    const size_t             mask_;
    std::unique_ptr<T[]>     buf_;
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
};

} // namespace southbridge

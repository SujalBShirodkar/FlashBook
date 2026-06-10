#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>

template<size_t N>
struct IsPowerOfTwo {
    static_assert((N & (N - 1)) == 0, "SPSCQueue capacity must be a power of 2");
    static constexpr bool value = true;
};

static constexpr size_t CACHE_LINE = 64;

template<typename T, size_t N>
class SPSCQueue {
    static_assert((N & (N-1)) == 0, "N must be a power of 2");
    static_assert(__is_trivially_copyable(T), "T must be trivially copyable");

public:
    SPSCQueue() : head_(0), tail_(0) {}

    bool push(const T& item) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next = (tail + 1) & MASK;

        if (next == head_.load(std::memory_order_acquire)) {
            return false;
        }

        slots_[tail] = item;

        tail_.store(next, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);

        if (head == tail_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }

        T item = slots_[head];

        head_.store((head + 1) & MASK, std::memory_order_release);
        return item;
    }
    
    size_t sizeApprox() const noexcept {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t t = tail_.load(std::memory_order_relaxed);
        return (t - h + N) & MASK;
    }

    bool emptyApprox() const noexcept {
        return head_.load(std::memory_order_relaxed)
            == tail_.load(std::memory_order_relaxed);
    }

    static constexpr size_t capacity() { return N; }

private:
    static constexpr size_t MASK = N - 1; 

    alignas(CACHE_LINE) std::atomic<size_t> head_;
    uint8_t _pad_head[CACHE_LINE - sizeof(std::atomic<size_t>)];

    alignas(CACHE_LINE) std::atomic<size_t> tail_;
    uint8_t _pad_tail[CACHE_LINE - sizeof(std::atomic<size_t>)];

    alignas(CACHE_LINE) std::array<T, N> slots_;
};
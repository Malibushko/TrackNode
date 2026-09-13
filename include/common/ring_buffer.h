#pragma once

#include <array>
#include <atomic>
#include <cstddef>

template <typename value_type, std::size_t capacity>
class ring_buffer
{
    static_assert(capacity > 0, "capacity must be greater than zero");

    static constexpr std::size_t buffer_size = capacity + 1;

public:
    ring_buffer() noexcept = default;

    bool push(const value_type& value) noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next_head = advance(head);

        if (next_head == tail_.load(std::memory_order_acquire))
        {
            return false;
        }

        buffer_[head] = value;
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    bool pop(value_type& out) noexcept
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire))
        {
            return false;
        }

        out = buffer_[tail];
        tail_.store(advance(tail), std::memory_order_release);
        return true;
    }

    bool empty() const noexcept
    {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    bool full() const noexcept
    {
        return advance(head_.load(std::memory_order_acquire)) == tail_.load(std::memory_order_acquire);
    }

    std::size_t size() const noexcept
    {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return (head >= tail) ? (head - tail) : (buffer_size - tail + head);
    }

    static constexpr std::size_t max_size() noexcept
    {
        return capacity;
    }

    void clear() noexcept
    {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

private:

    static constexpr std::size_t advance(std::size_t index) noexcept
    {
        ++index;
        return (index == buffer_size) ? 0 : index;
    }

    std::array<value_type, buffer_size> buffer_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};
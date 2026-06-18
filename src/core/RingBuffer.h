#pragma once
#include <atomic>
#include <cstddef>

template <typename T, size_t Size>
class RingBuffer {
private:
    T buffer[Size];
    
    // std::atomic ensures the CPU cores don't cache stale versions of these numbers
    // Head is where the Producer writes. Tail is where the Consumer reads.
    std::atomic<size_t> head{0}; 
    std::atomic<size_t> tail{0}; 

public:
    // Called ONLY by the Network Thread
    inline bool push(const T& item) {
        // memory_order_acquire prevents the CPU from reordering instructions 
        size_t current_tail = tail.load(std::memory_order_acquire);
        size_t current_head = head.load(std::memory_order_relaxed);
        
        size_t next_head = (current_head + 1) % Size;

        // If the next write position is the current read position, the buffer is FULL
        if (next_head == current_tail) {
            return false; 
        }

        buffer[current_head] = item;
        
        // memory_order_release guarantees the item is fully written before head updates
        head.store(next_head, std::memory_order_release);
        return true;
    }

    // Called ONLY by the Matching Engine Thread
    inline bool pop(T& item) {
        size_t current_head = head.load(std::memory_order_acquire);
        size_t current_tail = tail.load(std::memory_order_relaxed);

        // If the read position is the same as the write position, the buffer is EMPTY
        if (current_head == current_tail) {
            return false; 
        }

        item = buffer[current_tail];
        
        tail.store((current_tail + 1) % Size, std::memory_order_release);
        return true;
    }
};
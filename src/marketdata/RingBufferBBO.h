#pragma once
#include <atomic>
#include <optional>
#include "MarketData.h"

# define SIZE 1024 // 1024 Messages in ring at once 

class RingBufferBBO {
private:
    BboMessage buffer[SIZE];
    // head and tail set to 0 at the start
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0}; 

public:

    inline bool push(const BboMessage& msg){
        
        size_t current_tail = tail.load(std::memory_order_acquire);
        size_t current_head = head.load(std::memory_order_relaxed);

        size_t new_head = (current_head + 1) % SIZE;
        
        // Buffer is full 
        if (new_head == current_tail){
            return false;
        }

        buffer[current_head] = msg;

       head.store(new_head, std::memory_order_release);
       return true;

    }

    inline std::optional<BboMessage> pop() const {
        
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t current_head = head.load(std::memory_order_acquire);

        if (current_head == current_tail) {
            return std::nullopt;
        }

        BboMessage msg = buffer[current_tail];
        size_t new_tail = (current_tail + 1) % SIZE;
        tail.store(new_tail, std::memory_order_release);
        return msg;
    }
};
#pragma once
#include <vector>
#include "types.h" // We need this to know what an 'Order' looks like

class OrderPool {
private:
    std::vector<Order*> freeList;

public:
    // When the engine boots up, we pre-allocate a massive chunk of orders
    OrderPool(size_t poolSize = 100000) {
        // Reserve space so the vector doesn't resize itself
        freeList.reserve(poolSize);
        
        // Create 100,000 blank orders and put their addresses in our Free List
        for (size_t i = 0; i < poolSize; ++i) {
            // We just use dummy data (0) for now. We will overwrite it later.
            freeList.push_back(new Order(0, 0, 0, Side::BUY)); 
        }
    }

    // When the engine shuts down, we finally give the memory back to the OS
    ~OrderPool() {
        for (Order* order : freeList) {
            delete order;
        }
    }

    // This REPLACES the 'new' keyword
    Order* acquire(OrderId id, Price p, Quantity q, Side s) {
        if (freeList.empty()) {
            // Emergency fallback just in case we get a massive spike in orders
            return new Order(id, p, q, s);
        }

        // 1. Grab an empty order from the back of the list
        Order* order = freeList.back();
        freeList.pop_back();
        
        // 2. Overwrite the dummy data with the real order data
        order->id = id;
        order->price = p;
        order->quantity = q;
        order->side = s;
        
        // 3. Ensure pointers are clean before it goes into the book
        order->next = nullptr;
        order->prev = nullptr;
        
        return order;
    }

    // This REPLACES the 'delete' keyword
    void release(Order* order) {
        // Clean up the pointers so it doesn't accidentally link to old data
        order->next = nullptr;
        order->prev = nullptr;
        
        // Put the order back on the shelf to be reused!
        freeList.push_back(order);
    }
};
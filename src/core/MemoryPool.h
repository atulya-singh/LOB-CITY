#pragma once
#include <vector>
#include "types.h"

class OrderPool {
private:
    std::vector<Order*> freeList;
    std::vector<Order*> allAllocated; // Track everything for cleanup

public:
    OrderPool(size_t poolSize = 100000) {
        freeList.reserve(poolSize);
        allAllocated.reserve(poolSize);
        for (size_t i = 0; i < poolSize; ++i) {
            Order* o = new Order(0, 0, 0, Side::BUY);
            freeList.push_back(o);
            allAllocated.push_back(o);
        }
    }

    ~OrderPool() {
        for (Order* order : allAllocated) {
            delete order;
        }
    }

    Order* acquire(OrderId id, Price p, Quantity q, Side s, bool m = false) {
        Order* order;
        if (freeList.empty()) {
            order = new Order(id, p, q, s, m);
            allAllocated.push_back(order);
            return order;
        }

        order = freeList.back();
        freeList.pop_back();
        
        order->id = id;
        order->price = p;
        order->quantity = q;
        order->side = s;
        order->isMarket = m;
        order->next = nullptr;
        order->prev = nullptr;
        
        return order;
    }

    void release(Order* order) {
        order->next = nullptr;
        order->prev = nullptr;
        freeList.push_back(order);
    }
};
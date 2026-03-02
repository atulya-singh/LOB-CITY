#pragma once
#include "types.h"
#include <map>
#include <unordered_map>

// Forward declaration to break circular dependency
class OrderPool;

class OrderBook {
private:
    OrderPool* pool;
    std::map<Price, PriceLevel, std::greater<Price>> bids;
    std::map<Price, PriceLevel> asks;
    std::unordered_map<OrderId, Order*> orderMap;

    void matchBuyOrder(Order* buyOrder);
    void matchSellOrder(Order* sellOrder);
    void addOrder(Order* order);

public:
    OrderBook(OrderPool* p) : pool(p) {}

    void cancelOrder(OrderId id);
    void processOrder(Order* order);
};
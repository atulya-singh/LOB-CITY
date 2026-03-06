#pragma once
#include "types.h"
#include <map>
#include <unordered_map>
#include "UdpPublisher.h"
#include "LatencyTracker.h"
#include <vector>

// Forward declaration to break circular dependency
class OrderPool;

class OrderBook {
private:
    OrderPool* pool;
    UdpPublisher* udpPub;
    LatencyTracker latencyTracker;
    
    std::map<Price, PriceLevel, std::greater<Price>> bids;
    std::map<Price, PriceLevel> asks;
    std::unordered_map<OrderId, Order*> orderMap;
    std::vector<Trade> tradelog;

    void matchBuyOrder(Order* buyOrder);
    void matchSellOrder(Order* sellOrder);
    void addOrder(Order* order);
    void publishBBO();

public:
    OrderBook(OrderPool* p, UdpPublisher* u = nullptr) : pool(p), udpPub(u) {}

    void cancelOrder(OrderId id);
    void processOrder(Order* order);
    void display();

    void printLatencyReport()const{latencyTracker.printReport();}
};
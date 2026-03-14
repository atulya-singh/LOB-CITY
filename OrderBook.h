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
    std::vector<Trade> tradeLog;

    void matchBuyOrder(Order* buyOrder);
    void matchSellOrder(Order* sellOrder);
    void addOrder(Order* order);
    void publishBBO();
    void recordTrade(OrderId buyId, OrderId sellId, Price price, Quantity qty);

public:
    OrderBook(OrderPool* p, UdpPublisher* u = nullptr) : pool(p), udpPub(u) {
        tradeLog.reserve(1000000);
    }

    void cancelOrder(OrderId id);
    void processOrder(Order* order);
    void modifyOrder(OrderId id, OrderId newId, Price newPrice, Quantity newQty);
    void display();
    double getVWAP() const;
    const std::vector<Trade>& getTradeLog() const { return tradeLog; }
    void printMarketStats() const;

    void printLatencyReport()const{latencyTracker.printReport();}

     // --- BBO Accessors for Risk Engine ---
    // These let the OrderEntryGateway read the current top-of-book 
    // to feed into the RiskEngine's price collar checks.
    Price getBestBid() const { return bids.empty() ? 0 : bids.begin()->first; }
    Price getBestAsk() const { return asks.empty() ? 0 : asks.begin()->first; }
};
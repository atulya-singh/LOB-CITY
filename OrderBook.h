#pragma once
#include "types.h"
#include <map>
#include <unordered_map>
#include <iostream>
#include <algorithm> // for std::min

// 1. Forward Declaration
class OrderPool;

class OrderBook {
private:
    OrderPool* pool;
    std::map<Price, PriceLevel, std::greater<Price>> bids;
    std::map<Price, PriceLevel> asks;
    std::unordered_map<OrderId, Order*> orderMap;

    // We only DECLARE these inside the class now
    void matchBuyOrder(Order* buyOrder);
    void matchSellOrder(Order* sellOrder);
    void addOrder(Order* order);

public:
    OrderBook(OrderPool* p) : pool(p) {}

    void cancelOrder(OrderId id);
    void processOrder(Order* order);
};

// 2. NOW we include the full definition of the pool
#include "MemoryPool.h"

// 3. Move the implementation HERE using 'inline'
// This ensures the compiler knows what 'pool->release' is.

inline void OrderBook::matchBuyOrder(Order* buyOrder) {
    while (buyOrder->quantity > 0 && !asks.empty()) {
        auto bestAskIt = asks.begin();
        Price bestAskPrice = bestAskIt->first;
        PriceLevel& bestAskLevel = bestAskIt->second;

        if (buyOrder->price < bestAskPrice) break;

        Order* restingAsk = bestAskLevel.head;
        while (restingAsk != nullptr && buyOrder->quantity > 0) {
            Quantity tradeQty = std::min(buyOrder->quantity, restingAsk->quantity);
            buyOrder->quantity -= tradeQty;
            restingAsk->quantity -= tradeQty;

            std::cout << "[-] TRADE EXECUTED! Buyer: " << buyOrder->id 
                      << " | Seller: " << restingAsk->id 
                      << " | Price: " << bestAskPrice 
                      << " | Qty: " << tradeQty << "\n";

            Order* nextAsk = restingAsk->next;
            if (restingAsk->quantity == 0) {
                orderMap.erase(restingAsk->id);
                bestAskLevel.removeOrder(restingAsk);
                pool->release(restingAsk); // Now works!
            }
            restingAsk = nextAsk;
        }
        if (bestAskLevel.head == nullptr) asks.erase(bestAskIt);
    }
}

inline void OrderBook::matchSellOrder(Order* sellOrder) {
    while (sellOrder->quantity > 0 && !bids.empty()) {
        auto bestBidIt = bids.begin();
        Price bestBidPrice = bestBidIt->first;
        PriceLevel& bestBidLevel = bestBidIt->second;

        if (sellOrder->price > bestBidPrice) break;

        Order* restingBid = bestBidLevel.head;
        while (restingBid != nullptr && sellOrder->quantity > 0) {
            Quantity tradeQty = std::min(sellOrder->quantity, restingBid->quantity);
            sellOrder->quantity -= tradeQty;
            restingBid->quantity -= tradeQty;

            std::cout << "[-] TRADE EXECUTED! Buyer: " << restingBid->id 
                      << " | Seller: " << sellOrder->id 
                      << " | Price: " << bestBidPrice 
                      << " | Qty: " << tradeQty << "\n";

            Order* nextBid = restingBid->next;
            if (restingBid->quantity == 0) {
                orderMap.erase(restingBid->id);
                bestBidLevel.removeOrder(restingBid);
                pool->release(restingBid); // Now works!
            }
            restingBid = nextBid;
        }
        if (bestBidLevel.head == nullptr) bids.erase(bestBidIt);
    }
}

inline void OrderBook::addOrder(Order* order) {
    orderMap[order->id] = order;
    if (order->side == Side::BUY) {
        bids[order->price].appendOrder(order);
    } else {
        asks[order->price].appendOrder(order);
    }
}

inline void OrderBook::cancelOrder(OrderId id) {
    auto it = orderMap.find(id);
    if (it == orderMap.end()) return;

    Order* order = it->second;
    orderMap.erase(it);

    if (order->side == Side::BUY) {
        bids[order->price].removeOrder(order);
        if (bids[order->price].head == nullptr) bids.erase(order->price);
    } else {
        asks[order->price].removeOrder(order);
        if (asks[order->price].head == nullptr) asks.erase(order->price);
    }
    pool->release(order); // Now works!
}

inline void OrderBook::processOrder(Order* order) {
    if (order->side == Side::BUY) {
        matchBuyOrder(order);
    } else {
        matchSellOrder(order);
    }

    if (order->quantity > 0) {
        addOrder(order);
    } else {
        pool->release(order); // Now works!
    }
}
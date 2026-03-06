#include "OrderBook.h"
#include "MemoryPool.h"
#include "LatencyTracker.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <iomanip>

// Uncomment the line below if you want to see trade messages again
// #define ENABLE_LOGGING 
void OrderBook::recordTrade(OrderId buyId, OrderId sellId, Price price, Quantity qty){
    auto now = std::chrono::high_resolution_clock::now();
    u_int64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
        tradeLog.push_back({buyId, sellId, price, qty, ts});
}

void OrderBook::matchBuyOrder(Order* buyOrder) {
    while (buyOrder->quantity > 0 && !asks.empty()) {
        auto bestAskIt = asks.begin();
        PriceLevel& bestAskLevel = bestAskIt->second;

        if (!buyOrder->isMarket && buyOrder->price < bestAskIt->first) break;

        Order* restingAsk = bestAskLevel.head;
        while (restingAsk != nullptr && buyOrder->quantity > 0) {
            Quantity tradeQty = std::min(buyOrder->quantity, restingAsk->quantity);
            
            buyOrder->quantity -= tradeQty;
            restingAsk->quantity -= tradeQty;
            recordTrade(buyOrder->id, restingAsk->id, bestAskIt->first, tradeQty);
            bestAskLevel.totalVolume -= tradeQty;

#ifdef ENABLE_LOGGING
            std::cout << "[-] TRADE: Buyer " << buyOrder->id << " filled " << tradeQty 
                      << " @ " << bestAskIt->first << " against Seller " << restingAsk->id << "\n";
#endif

            Order* nextAsk = restingAsk->next;
            if (restingAsk->quantity == 0) {
                orderMap.erase(restingAsk->id);
                bestAskLevel.removeOrder(restingAsk);
                pool->release(restingAsk);
            }
            restingAsk = nextAsk;
        }
        if (bestAskLevel.head == nullptr) asks.erase(bestAskIt);
    }
}

void OrderBook::matchSellOrder(Order* sellOrder) {
    while (sellOrder->quantity > 0 && !bids.empty()) {
        auto bestBidIt = bids.begin();
        PriceLevel& bestBidLevel = bestBidIt->second;

        if (!sellOrder->isMarket && sellOrder->price > bestBidIt->first) break;

        Order* restingBid = bestBidLevel.head;
        while (restingBid != nullptr && sellOrder->quantity > 0) {
            Quantity tradeQty = std::min(sellOrder->quantity, restingBid->quantity);
            
            sellOrder->quantity -= tradeQty;
            restingBid->quantity -= tradeQty;
            recordTrade(restingBid->id, sellOrder->id, bestBidIt->first, tradeQty);
            bestBidLevel.totalVolume -= tradeQty;

#ifdef ENABLE_LOGGING
            std::cout << "[-] TRADE: Seller " << sellOrder->id << " filled " << tradeQty 
                      << " @ " << bestBidIt->first << " against Buyer " << restingBid->id << "\n";
#endif

            Order* nextBid = restingBid->next;
            if (restingBid->quantity == 0) {
                orderMap.erase(restingBid->id);
                bestBidLevel.removeOrder(restingBid);
                pool->release(restingBid);
            }
            restingBid = nextBid;
        }
        if (bestBidLevel.head == nullptr) bids.erase(bestBidIt);
    }
}

void OrderBook::addOrder(Order* order) {
    orderMap[order->id] = order;
    if (order->side == Side::BUY) {
        bids.try_emplace(order->price, order->price).first->second.appendOrder(order);
    } else {
        asks.try_emplace(order->price, order->price).first->second.appendOrder(order);
    }
}

void OrderBook::processOrder(Order* order) {
    auto start = std::chrono::high_resolution_clock::now();

    Price bidBefore = bids.empty() ? 0: bids.begin()->first;
    Price askBefore = asks.empty() ? 0: asks.begin()->first;
    if (order->side == Side::BUY) matchBuyOrder(order);
    else matchSellOrder(order);

    if(order->quantity >0){
        if(order->isMarket){
            pool->release(order);
        }else{
            addOrder(order);
        }
    }else{
        pool->release(order);
    }
    Price bidAfter = bids.empty() ? 0: bids.begin()->first;
    Price askAfter = asks.empty() ? 0 : asks.begin()->first;
    if(bidBefore != bidAfter || askBefore != askAfter){
        publishBBO();
    }
    auto end = std::chrono::high_resolution_clock::now();
    latencyTracker.record(
        std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count(),
        std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count()
    );
}

void OrderBook::cancelOrder(OrderId id) {
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
    pool->release(order);

    publishBBO();
}

void OrderBook::display() {
    std::cout << "\n========== ORDER BOOK (Top 5) ==========\n";
    std::cout << std::setw(12) << "ASK QTY"
              << std::setw(14) << "PRICE"
              << "\n";

    // Collect top 5 asks (ascending — lowest ask first, print reversed)
    std::vector<std::pair<Price,Quantity>> askLevels;
    int count = 0;
    for (auto& [px, level] : asks) {
        if (count++ == 5) break;
        askLevels.push_back({px, level.totalVolume});
    }
    for (auto it = askLevels.rbegin(); it != askLevels.rend(); ++it) {
        std::cout << std::setw(12) << it->second
                  << std::setw(14) << std::fixed << std::setprecision(4)
                  << (static_cast<double>(it->first) / 10000.0) << "\n";
    }

    std::cout << "----------------------------------------\n";

    // Top 5 bids (descending — highest bid first)
    std::cout << std::setw(12) << "BID QTY"
              << std::setw(14) << "PRICE"
              << "\n";
    count = 0;
    for (auto& [px, level] : bids) {
        if (count++ == 5) break;
        std::cout << std::setw(12) << level.totalVolume
                  << std::setw(14) << std::fixed << std::setprecision(4)
                  << (static_cast<double>(px) / 10000.0) << "\n";
    }
    std::cout << "=========================================\n\n";
}
void OrderBook::publishBBO() {
    if (!udpPub) return; // Safety check in case we run a test without the publisher

    BboMessage msg;
    msg.messageType = 'B';
    
    // Get the current time in nanoseconds
    auto now = std::chrono::high_resolution_clock::now();
    msg.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

    // Grab the Best Bid (Highest price in the descending map)
    if (!bids.empty()) {
        auto bestBid = bids.begin();
        msg.bestBidPrice = bestBid->first;
        msg.bestBidQty = bestBid->second.totalVolume;
    } else {
        msg.bestBidPrice = 0;
        msg.bestBidQty = 0;
    }

    // Grab the Best Ask (Lowest price in the ascending map)
    if (!asks.empty()) {
        auto bestAsk = asks.begin();
        msg.bestAskPrice = bestAsk->first;
        msg.bestAskQty = bestAsk->second.totalVolume;
    } else {
        msg.bestAskPrice = 0;
        msg.bestAskQty = 0;
    }

    // Blast it to the network! (Zero allocations, just raw bytes)
    udpPub->publishBbo(msg);
}
double OrderBook::getVWAP() const {
    if (tradeLog.empty()) return 0.0;
    double cumulativePxQty = 0.0;
    double cumulativeQty   = 0.0;
    for (const Trade& t : tradeLog) {
        double px = static_cast<double>(t.price) / 10000.0; // undo 4 decimal scaling
        cumulativePxQty += px * t.quantity;
        cumulativeQty   += t.quantity;
    }
    return cumulativePxQty / cumulativeQty;
}
void OrderBook::printMarketStats() const {
    std::cout << "\n========== MARKET STATS ==========\n";
    std::cout << "Total Trades Executed : " << tradeLog.size() << "\n";

    uint64_t totalQty = 0;
    for (const Trade& t : tradeLog) totalQty += t.quantity;
    std::cout << "Total Volume Traded   : " << totalQty << " units\n";
    std::cout << "VWAP                  : " << std::fixed << std::setprecision(4)
              << getVWAP() << "\n";

    if (!bids.empty() && !asks.empty()) {
        double bid = static_cast<double>(bids.begin()->first)  / 10000.0;
        double ask = static_cast<double>(asks.begin()->first) / 10000.0;
        std::cout << "Best Bid              : " << bid << "\n";
        std::cout << "Best Ask              : " << ask << "\n";
        std::cout << "Spread                : " << (ask - bid) << "\n";
        std::cout << "Mid Price             : " << ((bid + ask) / 2.0) << "\n";
    }
    std::cout << "===================================\n\n";
}
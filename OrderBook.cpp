#include "OrderBook.h"
#include "MemoryPool.h"
#include "LatencyTracker.h"
#include <iostream>
#include <algorithm>
#include <chrono>

// Uncomment the line below if you want to see trade messages again
// #define ENABLE_LOGGING 

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
        if (bids.find(order->price) == bids.end()) bids[order->price] = PriceLevel(order->price);
        bids[order->price].appendOrder(order);
    } else {
        if (asks.find(order->price) == asks.end()) asks[order->price] = PriceLevel(order->price);
        asks[order->price].appendOrder(order);
    }
}

void OrderBook::processOrder(Order* order) {
    auto start = std::chrono::high_resolution_clock::now();
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
    publishBBO();

    auto end = std::chrono::high_resolution_clock::now();
    uint64_t startNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        start.time_since_epoch()).count();
    uint64_t endNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end.time_since_epoch()).count();
    latencyTracker.record(startNs, endNs);


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
    // Keep this empty or commented out during the benchmark 
    // to ensure 0% I/O overhead.
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
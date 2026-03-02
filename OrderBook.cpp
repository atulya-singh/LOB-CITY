#include "OrderBook.h"
#include "MemoryPool.h"
#include <iostream>
#include <algorithm>

void OrderBook::matchBuyOrder(Order* buyOrder) {
    while (buyOrder->quantity > 0 && !asks.empty()) {
        auto bestAskIt = asks.begin();
        PriceLevel& bestAskLevel = bestAskIt->second;

        if (buyOrder->price < bestAskIt->first) break;

        Order* restingAsk = bestAskLevel.head;
        while (restingAsk != nullptr && buyOrder->quantity > 0) {
            Quantity tradeQty = std::min(buyOrder->quantity, restingAsk->quantity);
            
            buyOrder->quantity -= tradeQty;
            restingAsk->quantity -= tradeQty;
            bestAskLevel.totalVolume -= tradeQty; // Keep volume in sync

            std::cout << "[-] TRADE: Buyer " << buyOrder->id << " filled " << tradeQty 
                      << " @ " << bestAskIt->first << " against Seller " << restingAsk->id << "\n";

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

        if (sellOrder->price > bestBidIt->first) break;

        Order* restingBid = bestBidLevel.head;
        while (restingBid != nullptr && sellOrder->quantity > 0) {
            Quantity tradeQty = std::min(sellOrder->quantity, restingBid->quantity);
            
            sellOrder->quantity -= tradeQty;
            restingBid->quantity -= tradeQty;
            bestBidLevel.totalVolume -= tradeQty;

            std::cout << "[-] TRADE: Seller " << sellOrder->id << " filled " << tradeQty 
                      << " @ " << bestBidIt->first << " against Buyer " << restingBid->id << "\n";

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
    if (order->side == Side::BUY) matchBuyOrder(order);
    else matchSellOrder(order);

    if (order->quantity > 0) addOrder(order);
    else pool->release(order);
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
}
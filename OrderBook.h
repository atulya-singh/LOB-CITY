#pragma once
#include "types.h"
#include <map>
#include <unordered_map>
#include <iostream>
struct Order{
    OrderId id;
    Price price;
    Quantity quantity;
    Side side;

    Order* next;
    Order* prev;

    Order(OrderId id_, Price p_, Quantity q_, Side s_)
    : id(id_), price(p_), quantity(q_), side(s_), next(nullptr), prev(nullptr){}
};

struct PriceLevel{
    Price price;
    Order* head;
    Order* tail;
    Quantity totalVolume;

    PriceLevel() : price(0), head(nullptr), tail(nullptr), totalVolume(0){}

    PriceLevel(Price p_): price(p_), head(nullptr), tail(nullptr), totalVolume(0){}

    void appendOrder(Order* order){
        if(head == nullptr){
            head = order;
            tail = order;
        }else{
            tail->next = order;
            order->prev = tail;
            tail = order;

        }
        totalVolume += order->quantity;
    }

    void removeOrder(Order* order){
        if(order->prev != nullptr){
            order->prev->next = order->next;
        }else{
            head = order->next;
        }

        if(order->next != nullptr){
            order->next->prev = order->prev;
        }else{
            tail = order->prev;
        }
        totalVolume -= order->quantity;
        order->next = nullptr;
        order->prev = nullptr;
    }
};

class OrderBook{
    private:
    std ::map<Price, PriceLevel, std::greater<Price>> bids;

    std::map<Price, PriceLevel> asks;

    std:: unordered_map<OrderId, Order*> orderMap;

    void matchBuyOrder(Order* buyOrder){
        while(buyOrder->quantity >0 && !asks.empty()){
            auto bestAskIt = asks.begin();
            Price bestAskPrice = bestAskIt->first;
            PriceLevel& bestAskLevel = bestAskIt->second;

            if (buyOrder->price < bestAskPrice){
                break;
            }
            Order* restingAsk = bestAskLevel.head;

            while(restingAsk != nullptr && buyOrder->quantity > 0){
                Quantity tradeQty = std::min(buyOrder->quantity, restingAsk->quantity);

                buyOrder->quantity -= tradeQty;
                restingAsk->quantity -= tradeQty;
                
                std::cout << "[-] TRADE EXECUTED! Buyer: " << buyOrder->id 
          << " | Seller: " << restingAsk->id 
          << " | Price: " << bestAskPrice 
          << " | Qty: " << tradeQty << "\n";
                Order* nextAsk = restingAsk->next;

                if(restingAsk->quantity == 0){
                    orderMap.erase(restingAsk->id);
                    bestAskLevel.removeOrder(restingAsk);

                    //memory pool cleanup will come here later
                }

                restingAsk = nextAsk;
            }
            if (bestAskLevel.head == nullptr){
                asks.erase(bestAskIt);
            }

        }
    }

    void matchSellOrder(Order* sellOrder){
        while( sellOrder->quantity > 0 && !bids.empty()){
            auto bestBidIt = bids.begin();
            Price bestBidPrice = bestBidIt->first;
            PriceLevel& bestBidLevel = bestBidIt->second;

            if(sellOrder->price > bestBidPrice){
                break;
            }

            Order* restingBid = bestBidLevel.head;

            while(restingBid != nullptr && sellOrder->quantity > 0){
                Quantity tradeQty = std::min(sellOrder->quantity, restingBid->quantity);

                sellOrder->quantity -= tradeQty;
                restingBid->quantity -= tradeQty;

                std::cout << "[-] TRADE EXECUTED! Buyer: " << restingBid->id 
          << " | Seller: " << sellOrder->id 
          << " | Price: " << bestBidPrice 
          << " | Qty: " << tradeQty << "\n";

                Order* nextBid = restingBid->next;

                if(restingBid->quantity == 0){
                    orderMap.erase(restingBid->id);
                    bestBidLevel.removeOrder(restingBid);
                    //memory pool cleanup will come here later
                }
                restingBid = nextBid;

            }
            if(bestBidLevel.head == nullptr){
                bids.erase(bestBidIt);
            }
        }
    }

    public:
    OrderBook() = default;

    void addOrder(Order* order){
        orderMap[order->id] = order;

        if(order->side == Side :: BUY){
            bids[order->price].appendOrder(order);
        }else{
            asks[order->price].appendOrder(order);
        }
    }

    void cancelOrder(OrderId id){
        auto it = orderMap.find(id);
        if(it == orderMap.end()){
            return;
        }

        Order* order = it->second;
        orderMap.erase(it);

        if(order->side == Side::BUY){
            bids[order->price].removeOrder(order);

            if(bids[order->price].head == nullptr)
{
             bids.erase(order->price);
}        }else{
    asks[order->price].removeOrder(order);
    if(asks[order->price].head == nullptr){
        asks.erase(order->price);
    }
}
    }

public:
      void processOrder(Order* order){
        if(order->side == Side::BUY){
            matchBuyOrder(order);
        }else{
            matchSellOrder(order);
        }

        if(order->quantity > 0){
            addOrder(order);
        }else{
            //In the future, we will return this pointer to the memory pool.
        }
      }
};
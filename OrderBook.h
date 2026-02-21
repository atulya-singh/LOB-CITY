#pragma once
#include "types.h"
#include <map>
#include <unordered_map>
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
        }
      }
};
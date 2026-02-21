#pragma once
#include "types.h"

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
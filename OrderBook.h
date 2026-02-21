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
};
#pragma once 
#include <cstdint>

using Price = int64_t;
using Quantity = uint32_t;
using OrderId = uint64_t;

enum class Side{
    BUY, 
    SELL
};
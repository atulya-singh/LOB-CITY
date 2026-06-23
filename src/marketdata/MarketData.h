#pragma once
#include <cstdint>

struct alignas(64) BboMessage{
    uint64_t timestamp;
    int64_t  bestBidPrice;
    int64_t  bestAskPrice;
    uint32_t bestBidQty;
    uint32_t bestAskQty;
    char     messageType;
};
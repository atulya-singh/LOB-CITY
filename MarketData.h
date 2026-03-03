#pragma once
#include <cstdint>

#pragma pack(push, 1)

struct BboMessage{
    char messageType;
    uint64_t timestamp;
    int64_t bestBidPrice;
    uint32_t bestBidQty;
    int16_t bestAskPrice;
    uint32_t bestAskQty;
};
#pragma pack(drop)
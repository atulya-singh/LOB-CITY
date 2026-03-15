#pragma once
#include <cstdint>
#include <cmath>
#include <vector>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include "types.h"
#include "MemoryPool.h"
#include "OrderBook.h"


//MARKET MAKER CONFIGURATION
struct MarketMakerConfig {
    Price halfSpread = 5000;
    Quantity quoteSize = 50;

    int64_t maxposition = 500;
    Price skewPerUnit = 100;

    int64_t maxLoss = 50000000;

    OrderId startingId = 5000000;
};
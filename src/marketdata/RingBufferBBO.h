#pragma once
#include <atomic>
#include "MarketData.h"

# define SIZE 1024 // 1024 Messages in ring at once 

class RingBufferBBO {
private:
    BboMessage buffer[SIZE];
};
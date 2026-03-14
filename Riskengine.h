#pragma once
#include <cstdint>
#include <chrono>
#include <iostream>
#include <iomanip>
#include "types.h"
#include "FixParser.h"

enum class RiskRejectReason : uint8_t{
    NONE = 0, //ORDER PASSED All checks
    INVALID_PRICE,//Price <=0 on a limit order 
    INVALID_QUANTITY, // Quantity <=0
    FAT_FINGER_SIZE, //Quantity exceeds maximum allowed per order
    FAT_FINGER_NOTIONAL, // Price * qty exceeds maximum dollar exposure
    PRICE_COLLAR, // Price deviates too far from the current market
    RATE_LIMIT // Client sending too many messages per time window
};

// Convert reject reason to a human-readable string for logging
inline const char* rejectReasonToString(RiskRejectReason r) {
    switch (r) {
        case RiskRejectReason::NONE:              return "NONE";
        case RiskRejectReason::INVALID_PRICE:     return "INVALID_PRICE";
        case RiskRejectReason::INVALID_QUANTITY:   return "INVALID_QUANTITY";
        case RiskRejectReason::FAT_FINGER_SIZE:   return "FAT_FINGER_SIZE";
        case RiskRejectReason::FAT_FINGER_NOTIONAL: return "FAT_FINGER_NOTIONAL";
        case RiskRejectReason::PRICE_COLLAR:      return "PRICE_COLLAR";
        case RiskRejectReason::RATE_LIMIT:        return "RATE_LIMIT";
        default:                                   return "UNKNOWN";
    }
}
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

struct RiskConfg{
    //fat finger checks
    Quantity maxOrderQty = 1000000; // Max shares per single order
    int64_t maxNotional = 100000000000LL; // Max price * qty (in fixed point units)

    //price collar
    int64_t collarBps = 500;

    //rate limiting 
    u_int32_t maxMsgsPerWindow = 5000;
    u_int64_t windowDurationNs = 1000000000ULL; // 1 second in nanoseconds
};

//RISK STATISTICS
// Tracks how many orders were checked, passed and rejected by category.
// This is what you'd show on a monitoring dashboard in production.
// ================================================================
struct RiskStats {
    uint64_t totalChecked     = 0;
    uint64_t totalPassed      = 0;
    uint64_t rejectInvalidPx  = 0;
    uint64_t rejectInvalidQty = 0;
    uint64_t rejectFatFinger  = 0;
    uint64_t rejectNotional   = 0;
    uint64_t rejectCollar     = 0;
    uint64_t rejectRateLimit  = 0;
 
    void recordReject(RiskRejectReason reason) {
        switch (reason) {
            case RiskRejectReason::INVALID_PRICE:      rejectInvalidPx++;  break;
            case RiskRejectReason::INVALID_QUANTITY:    rejectInvalidQty++; break;
            case RiskRejectReason::FAT_FINGER_SIZE:    rejectFatFinger++;  break;
            case RiskRejectReason::FAT_FINGER_NOTIONAL:rejectNotional++;   break;
            case RiskRejectReason::PRICE_COLLAR:       rejectCollar++;     break;
            case RiskRejectReason::RATE_LIMIT:         rejectRateLimit++;  break;
            default: break;
        }
    }
};

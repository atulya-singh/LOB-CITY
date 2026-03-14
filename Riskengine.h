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

//RISK ENGINE 
class RiskEngine {
    private:
        RiskConfg config;
        RiskStats stats;
        //BBO Reference Prices
        //These are the "anchor" prices that collar checks compare against
        Price bestBid = 0;
        Price bestAsk = 0;

        //Rate limiter State
        // Tracks message count within a rolling time window.
    // In production, this would be per-client (keyed by session/clOrdID prefix).
    // For simplicity, we track a single global rate.
        u_int32_t windowMsgCount = 0;
        uint64_t windowStartNs = 0;


        //INDIVIDUAL CHECK FUNCTIONS

        //CHECK 1 : BASIC VALIDITY
        inline RiskRejectReason checkValidity(const ParsedFixMessage& msg) const {
            if(msg.qty <= 0){
                return RiskRejectReason::INVALID_QUANTITY;
            }
            if (msg.ordType !='1' && msg.price <=0){
                return RiskRejectReason::INVALID_PRICE;
            }
            return RiskRejectReason::NONE;
        }


        //CHECK 2: FAT FINGER - Order Size 
        //prevents a single order from being absurdly large
        inline RiskRejectReason checkFatFingerSize(const ParsedFixMessage& msg) const{
            if(static_cast<Quantity>(msg.qty) > config.maxOrderQty){
                return RiskRejectReason::FAT_FINGER_SIZE;
            }
            return RiskRejectReason::NONE;
        }


        //CHECK 3 : FAT FINGER - NOTIONAL VALUE 
        inline RiskRejectReason checkNotional(const ParsedFixMessage& msg) const{
            if(msg.ordType == '1') return RiskRejectReason::NONE; //skip for market orders

            int64_t notional = msg.price * msg.qty;
            if(notional > config.maxNotional){
                return RiskRejectReason::FAT_FINGER_NOTIONAL;
            }
            return RiskRejectReason::NONE;
        }



};
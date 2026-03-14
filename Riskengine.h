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

struct RiskConfig{
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
        RiskConfig config;
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



        //CHECK 4 : PRICE COLLAR 
        //Prevents orders from executing at prices far from the current market
        //cancels orders outside the band of the midpoint
        //midpoingt : (bestBid + bestAsk)/2
       inline RiskRejectReason checkPriceCollar(const ParsedFixMessage& msg) const {
        if (msg.ordType == '1') return RiskRejectReason::NONE; // Market orders have no price
        
        // Calculate the reference price from current BBO
        Price refPrice = 0;
        if (bestBid > 0 && bestAsk > 0) {
            refPrice = (bestBid + bestAsk) / 2;    // Midpoint
        } else if (bestBid > 0) {
            refPrice = bestBid;                      // Only bids on the book
        } else if (bestAsk > 0) {
            refPrice = bestAsk;                      // Only asks on the book
        } else {
            return RiskRejectReason::NONE;           // Empty book — no reference, allow order
        }
 
        // Calculate maximum allowed deviation
        // refPrice is in fixed-point (e.g., $150.00 = 1500000)
        // collarBps is in basis points (e.g., 500 = 5%)
        // maxDeviation = refPrice * 500 / 10000 = refPrice * 0.05
        Price maxDeviation = refPrice * config.collarBps / 10000;
 
        // Absolute deviation of the order price from the reference
        Price deviation = msg.price > refPrice 
                        ? (msg.price - refPrice) 
                        : (refPrice - msg.price);
 
        if (deviation > maxDeviation) {
            return RiskRejectReason::PRICE_COLLAR;
        }
        return RiskRejectReason::NONE;
    }


    //CHECK 5 : Rate limitng 
    //Prevents a single client from flooding the engine
    inline RiskRejectReason checkRateLimit() {
        uint64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
 
        // If we've moved past the current window, start a new one
        if (nowNs - windowStartNs >= config.windowDurationNs) {
            windowStartNs = nowNs;
            windowMsgCount = 0;
        }
 
        windowMsgCount++;
 
        if (windowMsgCount > config.maxMsgsPerWindow) {
            return RiskRejectReason::RATE_LIMIT;
        }
        return RiskRejectReason::NONE;
    }
public:
    // Constructor — takes an optional config, defaults to sensible values
    RiskEngine() : config() {}
    RiskEngine(const RiskConfig& cfg) : config(cfg) {}
 
    // --------------------------------------------------------
    // THE MAIN ENTRY POINT
    // Called by OrderEntryGateway for every incoming order.
    // Runs all checks in sequence — fails fast on the first rejection.
    //
    // Returns NONE if the order passes, or the specific reject reason.
    // --------------------------------------------------------
    inline RiskRejectReason checkOrder(const ParsedFixMessage& msg) {
        stats.totalChecked++;
 
        // --- Run checks from cheapest to most expensive ---
 
        RiskRejectReason result;
 
        result = checkValidity(msg);
        if (result != RiskRejectReason::NONE) {
            stats.recordReject(result);
            return result;
        }
 
        result = checkFatFingerSize(msg);
        if (result != RiskRejectReason::NONE) {
            stats.recordReject(result);
            return result;
        }
 
        result = checkNotional(msg);
        if (result != RiskRejectReason::NONE) {
            stats.recordReject(result);
            return result;
        }
 
        result = checkPriceCollar(msg);
        if (result != RiskRejectReason::NONE) {
            stats.recordReject(result);
            return result;
        }
 
        result = checkRateLimit();
        if (result != RiskRejectReason::NONE) {
            stats.recordReject(result);
            return result;
        }
 
        stats.totalPassed++;
        return RiskRejectReason::NONE;
    }
 
    // --------------------------------------------------------
    // BBO UPDATE
    // Called by the gateway after the OrderBook processes an order.
    // Keeps the risk engine's reference prices in sync with the market.
    // --------------------------------------------------------
    inline void updateBBO(Price bid, Price ask) {
        bestBid = bid;
        bestAsk = ask;
    }
 
    // --------------------------------------------------------
    // CONFIG ACCESS
    // Allows runtime tuning of risk parameters.
    // --------------------------------------------------------
    RiskConfig& getConfig() { return config; }
    const RiskConfig& getConfig() const { return config; }
 
    // --------------------------------------------------------
    // STATISTICS REPORT
    // --------------------------------------------------------
    void printReport() const {
        std::cout << "\n========== RISK ENGINE REPORT ==========\n";
        std::cout << "Total Checked     : " << stats.totalChecked << "\n";
        std::cout << "Total Passed      : " << stats.totalPassed  << "\n";
        std::cout << "Total Rejected    : " << (stats.totalChecked - stats.totalPassed) << "\n";
        
        if (stats.totalChecked > stats.totalPassed) {
            std::cout << "--- Rejection Breakdown ---\n";
            if (stats.rejectInvalidPx  > 0) std::cout << "  Invalid Price   : " << stats.rejectInvalidPx  << "\n";
            if (stats.rejectInvalidQty > 0) std::cout << "  Invalid Qty     : " << stats.rejectInvalidQty << "\n";
            if (stats.rejectFatFinger  > 0) std::cout << "  Fat Finger Size : " << stats.rejectFatFinger  << "\n";
            if (stats.rejectNotional   > 0) std::cout << "  Fat Finger $    : " << stats.rejectNotional   << "\n";
            if (stats.rejectCollar     > 0) std::cout << "  Price Collar    : " << stats.rejectCollar     << "\n";
            if (stats.rejectRateLimit  > 0) std::cout << "  Rate Limited    : " << stats.rejectRateLimit  << "\n";
        }
 
        if (stats.totalChecked > 0) {
            double passRate = 100.0 * stats.totalPassed / stats.totalChecked;
            std::cout << "Pass Rate         : " << std::fixed << std::setprecision(2) 
                      << passRate << "%\n";
        }
        std::cout << "=========================================\n\n";
    }


};
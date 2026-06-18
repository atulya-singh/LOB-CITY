#pragma once
#include <cstdint>
#include <string_view>

struct ParsedFixMessage {
    char msgType = '\0'; 
    char side = '\0';    
    char ordType = '\0'; 
    int64_t qty = 0;     
    int64_t price = 0;   
    std::string_view clOrdID;
    std::string_view origClOrdID; //TAG 41: Original order ID used in cancel/replace 
    
    void reset() {
        msgType = '\0';
        side = '\0';
        ordType = '\0';
        qty = 0;
        price = 0;
        clOrdID = std::string_view();
        origClOrdID = std::string_view();
    }
};

// Extremely fast ASCII-to-int for FIX Tags and Quantities
inline int64_t parseFastInt(const char* start, const char* end) {
    int64_t val = 0;
    for (const char* p = start; p < end; ++p) {
        val = val * 10 + (*p - '0');
    }
    return val;
}

// ASCII-to-int64_t for Prices (assuming 4 decimal places of precision)
inline int64_t parseFastDecimal(const char* start, const char* end) {
    int64_t val = 0;
    int impliedDecimals = 4; // 10^4 multiplier for precision
    int decimalCount = 0;
    bool seenDot = false;

    for (const char* p = start; p < end; ++p) {
        if (*p == '.') {
            seenDot = true;
            continue;
        }
        val = val * 10 + (*p - '0');
        if (seenDot) {
            decimalCount++;
            if (decimalCount == impliedDecimals) break;
        }
    }
    
    // Pad with zeros if the FIX price had fewer than 4 decimal places (e.g., "150.2")
    while (decimalCount < impliedDecimals) {
        val *= 10;
        decimalCount++;
    }
    return val;
}

bool parseFixMessage(const char* buffer, size_t length, ParsedFixMessage& outMsg);
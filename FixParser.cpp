#include "FixParser.h"

bool parseFixMessage(const char* buffer, size_t length, ParsedFixMessage& outMsg) {
    outMsg.reset();
    const char* ptr = buffer;
    const char* end = buffer + length;

    while (ptr < end) {
        // Find the '=' to isolate the Tag
        const char* tagEnd = ptr;
        while (tagEnd < end && *tagEnd != '=') {
            tagEnd++;
        }
        if (tagEnd == end) return false; // Malformed message

        // Parse the Tag using our custom function
        int tag = parseFastInt(ptr, tagEnd);
        
        ptr = tagEnd + 1; // Skip '='

        // Find the SOH character to isolate the Value
        const char* valueEnd = ptr;
        while (valueEnd < end && *valueEnd != '\x01') {
            valueEnd++;
        }

        // Process the Value based on the Tag
        switch (tag) {
            case 35: // MsgType
                if (ptr < valueEnd) outMsg.msgType = *ptr;
                break;
            case 54: // Side
                if (ptr < valueEnd) outMsg.side = *ptr;
                break;
            case 40: // OrdType
                if (ptr < valueEnd) outMsg.ordType = *ptr;
                break;
            case 38: // OrderQty
                outMsg.qty = parseFastInt(ptr, valueEnd);
                break;
            case 44: // Price
                outMsg.price = parseFastDecimal(ptr, valueEnd); 
                break;
            case 11: // ClOrdID
                outMsg.clOrdID = std::string_view(ptr, valueEnd - ptr);
                break;
            case 41: // OrigClOrdID (used in Cancel/Replace)
                outMsg.origCLOrdID = std::string_view(ptr, valueEnd - ptr);
                break;
            // Ignore unneeded tags
        }

        ptr = valueEnd + 1; // Move past SOH for the next tag
    }
    return true;
}
#pragma once
#include "types.h"
#include "MemoryPool.h"
#include "OrderBook.h"
#include "FixParser.h"
#include "Riskengine.h"

class OrderEntryGateway {
private:
    OrderPool* pool;
    OrderBook* book;
    RiskEngine risk; 

public:
    OrderEntryGateway(OrderPool* p, OrderBook* b, const RiskConfig& cfg = RiskConfig()) : pool(p), book(b), risk(cfg) {}

    inline void onParsedMessage(const ParsedFixMessage& msg) {
        if (msg.msgType == 'D') { // New Order Single
            handleNewOrderSingle(msg);
        } else if (msg.msgType == 'F') { // Order Cancel Request
            handleCancelRequest(msg);
        }
    }
    RiskEngine& getRiskEngine(){return risk;}
    const RiskEngine& getRiskEngine() const {return risk;}

private:
    inline void handleNewOrderSingle(const ParsedFixMessage& msg) {
        //STEP 0: First check perform the PRE-TRADE RISK CHECK
        //happens before we touch memory pool

        risk.updateBBO(book->getBestBid(), book->getBestAsk());
        RiskRejectReason rejection = risk.checkOrder(msg);
        if(rejection != RiskRejectReason::NONE){
#ifdef ENABLE_RISK_LOGGING
            std::cout << "[RISK] REJECTED Order " << msg.clOrdID 
                      << " | Reason: " << rejectReasonToString(rejection) << "\n";
#endif
            return; // <-- Order dies here. Never reaches the book.
        }
        
        
        // 1. Convert string_view clOrdID back to a fast int (uint64_t)
        // Since you already have parseFastInt in FixParser.h, we can reuse it!
        OrderId numericId = static_cast<OrderId>(
            parseFastInt(msg.clOrdID.data(), msg.clOrdID.data() + msg.clOrdID.length())
        );

        // 2. Map the enums
        Side orderSide = (msg.side == '1') ? Side::BUY : Side::SELL;
        bool isMarket = (msg.ordType == '1'); // In FIX, '1' is Market, '2' is Limit

        // 3. Acquire from your OrderPool (Zero Allocation)
        Order* newOrder = pool->acquire(
            numericId, 
            msg.price, 
            msg.qty, 
            orderSide, 
            isMarket
        );

        // 4. Dispatch to your Matching Engine
        book->processOrder(newOrder);
    }

    inline void handleCancelRequest(const ParsedFixMessage& msg) {
        // Convert the string_view ID to your uint64_t OrderId
        OrderId numericId = static_cast<OrderId>(
            parseFastInt(msg.clOrdID.data(), msg.clOrdID.data() + msg.clOrdID.length())
        );

        // Your OrderBook already handles the pool->release() inside cancelOrder!
        book->cancelOrder(numericId);
    }
};

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
    RiskEngine risk;     // <-- The gatekeeper. Every order goes through this first.

public:
    // Constructor now accepts an optional RiskConfig.
    // If you don't pass one, the RiskEngine uses sensible defaults.
    OrderEntryGateway(OrderPool* p, OrderBook* b, const RiskConfig& cfg = RiskConfig()) 
        : pool(p), book(b), risk(cfg) {}

    inline void onParsedMessage(const ParsedFixMessage& msg) {
        if (msg.msgType == 'D') { // New Order Single
            handleNewOrderSingle(msg);
        } else if (msg.msgType == 'F') { // Order Cancel Request
            handleCancelRequest(msg);
        } else if (msg.msgType == 'G') { // Order Cancel/Replace Request
            handleCancelReplace(msg);
        }
    }

    // --- Expose the risk engine for reporting and config tuning ---
    RiskEngine&       getRiskEngine()       { return risk; }
    const RiskEngine& getRiskEngine() const { return risk; }

private:
    inline void handleNewOrderSingle(const ParsedFixMessage& msg) {
        // ============================================================
        // STEP 0: PRE-TRADE RISK CHECK (NEW)
        // This happens BEFORE we touch the memory pool.
        // If the order is rejected, we never allocate, never match,
        // never publish — the bad order just vanishes here.
        // ============================================================

        // First, sync the risk engine's view of the market with the 
        // current order book BBO. This costs ~2ns (two map lookups)
        // but ensures collar checks use fresh reference prices.
        risk.updateBBO(book->getBestBid(), book->getBestAsk());

        RiskRejectReason rejection = risk.checkOrder(msg);
        if (rejection != RiskRejectReason::NONE) {
            // In production, you'd send a FIX Reject (35=3) or 
            // ExecutionReport (35=8) with OrdStatus=Rejected back 
            // to the client over the TCP socket. For now, we just
            // log it when logging is enabled.
#ifdef ENABLE_RISK_LOGGING
            std::cout << "[RISK] REJECTED Order " << msg.clOrdID 
                      << " | Reason: " << rejectReasonToString(rejection) << "\n";
#endif
            return; // <-- Order dies here. Never reaches the book.
        }

        // ============================================================
        // STEP 1-4: Original pipeline (unchanged)
        // ============================================================
        
        // 1. Convert string_view clOrdID back to a fast int (uint64_t)
        OrderId numericId = static_cast<OrderId>(
            parseFastInt(msg.clOrdID.data(), msg.clOrdID.data() + msg.clOrdID.length())
        );

        // 2. Map the FIX enum codes to internal types
        Side orderSide = (msg.side == '1') ? Side::BUY : Side::SELL;
        bool isMarket = (msg.ordType == '1');

        // 3. Acquire from the OrderPool (Zero Allocation)
        Order* newOrder = pool->acquire(
            numericId, 
            msg.price, 
            msg.qty, 
            orderSide, 
            isMarket
        );

        // 4. Dispatch to the Matching Engine
        book->processOrder(newOrder);
    }

    inline void handleCancelRequest(const ParsedFixMessage& msg) {
        // Cancels bypass the risk engine — they reduce risk, not increase it.
        // (In production, you might still rate-limit cancels to prevent
        // "cancel storms" that overload the engine.)
        OrderId numericId = static_cast<OrderId>(
            parseFastInt(msg.clOrdID.data(), msg.clOrdID.data() + msg.clOrdID.length())
        );
        book->cancelOrder(numericId);
    }

    inline void handleCancelReplace(const ParsedFixMessage& msg) {
        // ============================================================
        // FIX Cancel/Replace Request (MsgType 'G')
        //
        // FIX fields:
        //   Tag 41 (OrigClOrdID) = the existing order to modify
        //   Tag 11 (ClOrdID)     = the new ID for the modified order
        //   Tag 44 (Price)       = the new price
        //   Tag 38 (OrderQty)    = the new quantity
        //
        // Risk check: Cancel/Replace goes through risk because the
        // new price/quantity could violate collar or fat-finger limits.
        // However, we only check the NEW values — the original order
        // already passed risk when it was first submitted.
        // ============================================================

        risk.updateBBO(book->getBestBid(), book->getBestAsk());

        RiskRejectReason rejection = risk.checkOrder(msg);
        if (rejection != RiskRejectReason::NONE) {
#ifdef ENABLE_RISK_LOGGING
            std::cout << "[RISK] REJECTED Modify " << msg.origClOrdID 
                      << " -> " << msg.clOrdID
                      << " | Reason: " << rejectReasonToString(rejection) << "\n";
#endif
            return;  // Modification rejected — original order stays untouched
        }

        // Parse the IDs
        OrderId origId = static_cast<OrderId>(
            parseFastInt(msg.origClOrdID.data(), msg.origClOrdID.data() + msg.origClOrdID.length())
        );
        OrderId newId = static_cast<OrderId>(
            parseFastInt(msg.clOrdID.data(), msg.clOrdID.data() + msg.clOrdID.length())
        );

        book->modifyOrder(origId, newId, msg.price, static_cast<Quantity>(msg.qty));
    }
};
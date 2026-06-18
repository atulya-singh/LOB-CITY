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

// ================================================================
// MARKET MAKER CONFIGURATION
// ================================================================
struct MarketMakerConfig {
    // --- Quote Parameters ---
    Price halfSpread       = 5000;     // $0.50 in fixed-point → full spread = $1.00
    Quantity quoteSize     = 50;       // Shares on each side

    // --- Inventory Risk Management ---
    int64_t maxPosition    = 500;      // Hard cap on net position (long or short)
    Price skewPerUnit      = 100;      // Quote shift per share of inventory ($0.01)

    // --- Risk Limits ---
    int64_t maxLoss        = 50000000; // Kill switch at -$5,000 (fixed-point)

    // --- Order ID Management ---
    OrderId startingId     = 5000000;  
};

// ================================================================
// PNL SNAPSHOT — recorded every tick for post-trade analysis
// ================================================================
struct PnlSnapshot {
    uint64_t tickNumber;
    int64_t  netPosition;
    double   realizedPnl;
    double   unrealizedPnl;
    double   totalPnl;
    Price    midPrice;
};

// ================================================================
// THE MARKET MAKER
//
// Simplified Avellaneda-Stoikov style market maker:
//   1. Quote a bid and ask around the midpoint
//   2. Skew quotes based on inventory to mean-revert position
//   3. Pull quotes when position limits are hit
//   4. Kill switch on max loss
// ================================================================
class MarketMaker {
private:
    OrderPool* pool;
    OrderBook* book;
    MarketMakerConfig config;

    // --- State ---
    int64_t netPosition    = 0;
    double  realizedPnl    = 0.0;
    double  avgEntryPrice  = 0.0;
    bool    killed         = false;

    // --- Live Order Tracking ---
    OrderId currentBidId   = 0;
    OrderId currentAskId   = 0;
    Price   currentBidPx   = 0;
    Price   currentAskPx   = 0;
    OrderId nextOrderId;

    // --- Analytics ---
    std::vector<PnlSnapshot> pnlHistory;
    uint64_t tickCount         = 0;
    uint64_t totalFills        = 0;
    uint64_t totalQuoteUpdates = 0;

    // ============================================================
    // INVENTORY SKEW
    //
    // The core of market-making risk management.
    //
    // If you're long: shift both quotes DOWN.
    //   → Your bid becomes less attractive (fewer buys)
    //   → Your ask becomes more attractive (more sells to you)
    //   → Net effect: encourages the market to reduce your position
    //
    // If you're short: shift both quotes UP. Same logic, reversed.
    //
    // Math:
    //   skew = netPosition * skewPerUnit
    //   bid = mid - halfSpread - skew
    //   ask = mid + halfSpread - skew
    //
    // Example (mid=$150, halfSpread=$0.50, skew=$0.01/share):
    //   Flat (pos=0):    bid=$149.50, ask=$150.50  (symmetric)
    //   Long 100:        bid=$148.50, ask=$149.50  (shifted $1 down)
    //   Short 100:       bid=$150.50, ask=$151.50  (shifted $1 up)
    // ============================================================
    inline Price computeSkew() const {
        return netPosition * config.skewPerUnit;
    }

    // ============================================================
    // FILL DETECTION
    // Scan the trade log for trades involving our order IDs.
    // ============================================================
    void processFills(size_t tradeLogStartIdx) {
        const auto& trades = book->getTradeLog();
        
        for (size_t i = tradeLogStartIdx; i < trades.size(); ++i) {
            const Trade& t = trades[i];
            
            if (t.buyOrderId == currentBidId) {
                double fillPx = static_cast<double>(t.price) / 10000.0;
                updatePosition(static_cast<int64_t>(t.quantity), fillPx);
                totalFills++;
            }

            if (t.sellOrderId == currentAskId) {
                double fillPx = static_cast<double>(t.price) / 10000.0;
                updatePosition(-static_cast<int64_t>(t.quantity), fillPx);
                totalFills++;
            }
        }
    }

    // ============================================================
    // POSITION & PNL ACCOUNTING
    //
    // Average-cost method:
    //   Adding to position  → weighted-average the entry price
    //   Reducing position   → realize PnL at (exit - avg entry)
    //   Crossing zero       → realize on the closed portion,
    //                          start new position at fill price
    // ============================================================
    void updatePosition(int64_t qtyChange, double fillPrice) {
        if (netPosition == 0) {
            netPosition = qtyChange;
            avgEntryPrice = fillPrice;
            return;
        }

        bool sameDirection = (netPosition > 0 && qtyChange > 0) || 
                             (netPosition < 0 && qtyChange < 0);

        if (sameDirection) {
            double totalCost = avgEntryPrice * std::abs(netPosition) + 
                               fillPrice * std::abs(qtyChange);
            netPosition += qtyChange;
            avgEntryPrice = totalCost / std::abs(netPosition);
        } else {
            int64_t closeQty = std::min(std::abs(qtyChange), std::abs(netPosition));
            
            if (netPosition > 0) {
                realizedPnl += closeQty * (fillPrice - avgEntryPrice);
            } else {
                realizedPnl += closeQty * (avgEntryPrice - fillPrice);
            }

            int64_t oldPosition = netPosition;
            netPosition += qtyChange;

            if ((oldPosition > 0 && netPosition < 0) || 
                (oldPosition < 0 && netPosition > 0)) {
                avgEntryPrice = fillPrice;
            }
            if (netPosition == 0) {
                avgEntryPrice = 0.0;
            }
        }
    }

    double computeUnrealizedPnl(Price midPrice) const {
        if (netPosition == 0) return 0.0;
        double mid = static_cast<double>(midPrice) / 10000.0;
        if (netPosition > 0) {
            return netPosition * (mid - avgEntryPrice);
        } else {
            return std::abs(netPosition) * (avgEntryPrice - mid);
        }
    }

    void cancelExistingQuotes() {
        if (currentBidId != 0) {
            book->cancelOrder(currentBidId);
            currentBidId = 0;
            currentBidPx = 0;
        }
        if (currentAskId != 0) {
            book->cancelOrder(currentAskId);
            currentAskId = 0;
            currentAskPx = 0;
        }
    }

    void placeQuote(Price price, Quantity qty, Side side) {
        OrderId id = nextOrderId++;
        Order* order = pool->acquire(id, price, qty, side);
        book->processOrder(order);

        if (side == Side::BUY) {
            currentBidId = id;
            currentBidPx = price;
        } else {
            currentAskId = id;
            currentAskPx = price;
        }
        totalQuoteUpdates++;
    }

public:
    MarketMaker(OrderPool* p, OrderBook* b, const MarketMakerConfig& cfg = MarketMakerConfig())
        : pool(p), book(b), config(cfg), nextOrderId(cfg.startingId) 
    {
        pnlHistory.reserve(1000000);
    }

    // ============================================================
    // ON_TICK: called every time the market state updates
    // ============================================================
    void onTick(size_t tradeLogStartIdx) {
        tickCount++;

        Price bestBid = book->getBestBid();
        Price bestAsk = book->getBestAsk();

        if (bestBid == 0 || bestAsk == 0) return;

        Price midPrice = (bestBid + bestAsk) / 2;

        // Step 1: Detect fills
        processFills(tradeLogStartIdx);

        // Step 2: Check kill switch
        double unrealizedPnl = computeUnrealizedPnl(midPrice);
        double totalPnl = realizedPnl + unrealizedPnl;

        if (totalPnl * 10000.0 < -config.maxLoss && !killed) {
            killed = true;
            cancelExistingQuotes();
        }

        // Record snapshot
        pnlHistory.push_back({tickCount, netPosition, realizedPnl, 
                              unrealizedPnl, totalPnl, midPrice});

        if (killed) return;

        // Step 3: Compute skewed quotes
        Price skew = computeSkew();
        Price newBidPx = midPrice - config.halfSpread - skew;
        Price newAskPx = midPrice + config.halfSpread - skew;

        // Step 4: Position limit enforcement
        bool quoteBid = (netPosition < config.maxPosition);
        bool quoteAsk = (netPosition > -config.maxPosition);

        // Cap quote size to remaining room before the limit
        // This prevents overshoot: if we're at 480 with maxPosition=500,
        // we only quote 20 shares on the bid, not the full 50.
        Quantity bidSize = config.quoteSize;
        Quantity askSize = config.quoteSize;

        if (quoteBid) {
            int64_t room = config.maxPosition - netPosition;
            if (room < static_cast<int64_t>(bidSize)) {
                bidSize = static_cast<Quantity>(room);
            }
        }
        if (quoteAsk) {
            int64_t room = config.maxPosition + netPosition;
            if (room < static_cast<int64_t>(askSize)) {
                askSize = static_cast<Quantity>(room);
            }
        }

        // Step 5: Only requote if prices changed
        bool bidChanged = (newBidPx != currentBidPx) || (!quoteBid && currentBidId != 0);
        bool askChanged = (newAskPx != currentAskPx) || (!quoteAsk && currentAskId != 0);

        if (bidChanged || askChanged) {
            cancelExistingQuotes();
            if (quoteBid && newBidPx > 0 && bidSize > 0) placeQuote(newBidPx, bidSize, Side::BUY);
            if (quoteAsk && newAskPx > 0 && askSize > 0) placeQuote(newAskPx, askSize, Side::SELL);
        }
    }

    void shutdown() { cancelExistingQuotes(); }

    // ============================================================
    // ACCESSORS
    // ============================================================
    bool isKilled() const { return killed; }
    int64_t getPosition() const { return netPosition; }
    double getRealizedPnl() const { return realizedPnl; }
    const std::vector<PnlSnapshot>& getPnlHistory() const { return pnlHistory; }

    void printReport() const {
        Price finalMid = 0;
        if (book->getBestBid() > 0 && book->getBestAsk() > 0) {
            finalMid = (book->getBestBid() + book->getBestAsk()) / 2;
        }
        double unrealized = computeUnrealizedPnl(finalMid);
        double total = realizedPnl + unrealized;

        std::cout << "\n========== MARKET MAKER REPORT ==========\n";
        std::cout << "Ticks Processed    : " << tickCount << "\n";
        std::cout << "Total Fills        : " << totalFills << "\n";
        std::cout << "Quote Updates      : " << totalQuoteUpdates << "\n";
        std::cout << "Final Position     : " << netPosition << " shares\n";
        std::cout << "Avg Entry Price    : $" << std::fixed << std::setprecision(4) 
                  << avgEntryPrice << "\n";
        std::cout << "Realized PnL       : $" << std::fixed << std::setprecision(2) 
                  << realizedPnl << "\n";
        std::cout << "Unrealized PnL     : $" << std::fixed << std::setprecision(2) 
                  << unrealized << "\n";
        std::cout << "Total PnL          : $" << std::fixed << std::setprecision(2) 
                  << total << "\n";

        if (!pnlHistory.empty()) {
            double peak = pnlHistory[0].totalPnl;
            double maxDrawdown = 0.0;
            for (const auto& snap : pnlHistory) {
                if (snap.totalPnl > peak) peak = snap.totalPnl;
                double dd = peak - snap.totalPnl;
                if (dd > maxDrawdown) maxDrawdown = dd;
            }
            std::cout << "Max Drawdown       : $" << std::fixed << std::setprecision(2)
                      << maxDrawdown << "\n";

            if (totalFills > 0) {
                std::cout << "PnL per Fill       : $" << std::fixed << std::setprecision(4)
                          << (total / totalFills) << "\n";
            }

            int64_t maxPos = 0, minPos = 0;
            for (const auto& snap : pnlHistory) {
                if (snap.netPosition > maxPos) maxPos = snap.netPosition;
                if (snap.netPosition < minPos) minPos = snap.netPosition;
            }
            std::cout << "Max Long Position  : " << maxPos << " shares\n";
            std::cout << "Max Short Position : " << std::abs(minPos) << " shares\n";
        }

        std::cout << "Status             : " << (killed ? "KILLED" : "ACTIVE") << "\n";
        std::cout << "==========================================\n\n";
    }
};
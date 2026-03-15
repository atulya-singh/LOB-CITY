#include <iostream>
#include <fstream>
#include <cmath>
#include <random>
#include <iomanip>
#include "types.h"
#include "MemoryPool.h"
#include "OrderBook.h"
#include "Marketmaker.h"

// ================================================================
// ORDER FLOW SIMULATOR
//
// Generates synthetic but realistic market activity:
//   - Price follows a random walk (Brownian motion)
//   - Order sizes drawn from an exponential distribution
//   - Mix of limit orders, market orders, and cancels
//   - Order arrival follows a Poisson-like process
//
// The market maker runs alongside this flow, quoting and getting
// filled as the synthetic participants trade.
// ================================================================

// ================================================================
// SIMULATION CONFIG
// ================================================================
struct SimConfig {
    // --- Price Model ---
    double startPrice      = 150.0;     // Starting mid price ($)
    double volatility      = 0.0003;    // Per-tick volatility (std dev of returns)
                                         // 0.0003 ≈ 3 bps per tick

    // --- Order Flow ---
    int    numTicks        = 100000;    // Total simulation ticks
    double limitOrderProb  = 0.60;      // 60% of ticks generate a limit order
    double marketOrderProb = 0.15;      // 15% generate a market order
    double cancelProb      = 0.15;      // 15% generate a cancel
                                         // Remaining 10%: no order (quiet tick)

    // --- Order Characteristics ---
    double avgOrderSize    = 30.0;      // Mean order size (exponential distribution)
    int    spreadTicks     = 3;         // How many price levels deep limit orders go
    
    // --- Seeding ---
    int    seedDepthLevels = 10;        // Initial book depth (levels per side)
    int    seedQtyPerLevel = 200;       // Initial quantity at each level
    Price  seedTickSize    = 1000;      // $0.10 between initial levels
};

// ================================================================
// THE SIMULATOR
// ================================================================
class Simulator {
private:
    SimConfig simCfg;
    MarketMakerConfig mmCfg;

    std::mt19937 rng;
    std::normal_distribution<double> priceDist;
    std::exponential_distribution<double> sizeDist;
    std::uniform_real_distribution<double> uniformDist;

    double currentMidPrice;
    OrderId nextOrderId = 1;

    // Track outstanding limit orders for cancellation
    std::vector<OrderId> outstandingOrders;

public:
    Simulator(const SimConfig& sc = SimConfig(), 
              const MarketMakerConfig& mc = MarketMakerConfig(),
              uint64_t seed = 42)
        : simCfg(sc), mmCfg(mc),
          rng(seed),
          priceDist(0.0, sc.volatility),
          sizeDist(1.0 / sc.avgOrderSize),
          uniformDist(0.0, 1.0),
          currentMidPrice(sc.startPrice)
    {
        outstandingOrders.reserve(100000);
    }

    void run() {
        std::cout << "================================================================\n";
        std::cout << "  MARKET MAKER BACKTEST SIMULATOR\n";
        std::cout << "================================================================\n";
        std::cout << "Ticks: " << simCfg.numTicks 
                  << " | Start Price: $" << simCfg.startPrice
                  << " | Volatility: " << (simCfg.volatility * 10000) << " bps/tick\n";
        std::cout << "Half Spread: $" << std::fixed << std::setprecision(2) 
                  << (mmCfg.halfSpread / 10000.0)
                  << " | Quote Size: " << mmCfg.quoteSize
                  << " | Max Position: " << mmCfg.maxPosition << "\n";
        std::cout << "Skew: $" << std::fixed << std::setprecision(4) 
                  << (mmCfg.skewPerUnit / 10000.0) << "/share"
                  << " | Kill Switch: -$" << std::fixed << std::setprecision(0) 
                  << (mmCfg.maxLoss / 10000.0) << "\n";
        std::cout << "================================================================\n\n";

        // --- Setup ---
        OrderPool pool(2000000);
        OrderBook book(&pool);
        MarketMaker mm(&pool, &book, mmCfg);

        // --- Seed the initial book ---
        seedBook(book, pool);
        std::cout << "[SIM] Book seeded with " << simCfg.seedDepthLevels 
                  << " levels per side.\n";

        // --- Run simulation ---
        std::cout << "[SIM] Running " << simCfg.numTicks << " ticks...\n\n";

        int progressInterval = simCfg.numTicks / 10;

        for (int tick = 0; tick < simCfg.numTicks; ++tick) {
            // Progress bar
            if (progressInterval > 0 && tick % progressInterval == 0 && tick > 0) {
                int pct = (tick * 100) / simCfg.numTicks;
                double mid = currentMidPrice;
                std::cout << "[SIM] " << pct << "% | Mid: $" << std::fixed 
                          << std::setprecision(2) << mid
                          << " | MM Position: " << mm.getPosition()
                          << " | MM PnL: $" << std::fixed << std::setprecision(2) 
                          << mm.getRealizedPnl() << "\n";
            }

            // Record trade log position BEFORE this tick's orders
            size_t tradeLogBefore = book.getTradeLog().size();

            // Step 1: Evolve the "true" mid price (random walk)
            double returns = priceDist(rng);
            currentMidPrice *= (1.0 + returns);

            // Step 2: Generate a synthetic order
            generateOrder(book, pool);

            // Step 3: Let the market maker react
            mm.onTick(tradeLogBefore);

            if (mm.isKilled()) {
                std::cout << "[SIM] Market maker killed at tick " << tick << "\n";
                break;
            }
        }

        // --- Shutdown ---
        mm.shutdown();

        // --- Reports ---
        mm.printReport();
        book.printMarketStats();

        // --- Export PnL curve to CSV ---
        exportPnlCurve(mm, "pnl_curve.csv");
    }

private:
    // ============================================================
    // SEED THE BOOK
    // Create initial depth so the market maker has something to trade against
    // ============================================================
    void seedBook(OrderBook& book, OrderPool& pool) {
        Price midPx = static_cast<Price>(currentMidPrice * 10000);

        for (int i = 1; i <= simCfg.seedDepthLevels; ++i) {
            Price bidPx = midPx - i * simCfg.seedTickSize;
            Price askPx = midPx + i * simCfg.seedTickSize;
            
            OrderId bidId = nextOrderId++;
            OrderId askId = nextOrderId++;

            book.processOrder(pool.acquire(bidId, bidPx, simCfg.seedQtyPerLevel, Side::BUY));
            book.processOrder(pool.acquire(askId, askPx, simCfg.seedQtyPerLevel, Side::SELL));
            
            outstandingOrders.push_back(bidId);
            outstandingOrders.push_back(askId);
        }
    }

    // ============================================================
    // GENERATE A SINGLE SYNTHETIC ORDER
    // ============================================================
    void generateOrder(OrderBook& book, OrderPool& pool) {
        double roll = uniformDist(rng);
        Price midPx = static_cast<Price>(currentMidPrice * 10000);

        if (roll < simCfg.limitOrderProb) {
            // --- LIMIT ORDER ---
            generateLimitOrder(book, pool, midPx);
        } 
        else if (roll < simCfg.limitOrderProb + simCfg.marketOrderProb) {
            // --- MARKET ORDER ---
            generateMarketOrder(book, pool);
        }
        else if (roll < simCfg.limitOrderProb + simCfg.marketOrderProb + simCfg.cancelProb) {
            // --- CANCEL ---
            generateCancel(book);
        }
        // else: quiet tick — no order
    }

    void generateLimitOrder(OrderBook& book, OrderPool& pool, Price midPx) {
        Side side = (uniformDist(rng) < 0.5) ? Side::BUY : Side::SELL;
        
        // Place limit orders within a few ticks of the mid
        int tickOffset = static_cast<int>(uniformDist(rng) * simCfg.spreadTicks) + 1;
        Price price;
        if (side == Side::BUY) {
            price = midPx - tickOffset * simCfg.seedTickSize;
        } else {
            price = midPx + tickOffset * simCfg.seedTickSize;
        }
        if (price <= 0) return;

        Quantity qty = static_cast<Quantity>(sizeDist(rng) * simCfg.avgOrderSize) + 1;
        if (qty > 500) qty = 500;  // Cap synthetic order size

        OrderId id = nextOrderId++;
        book.processOrder(pool.acquire(id, price, qty, side));
        outstandingOrders.push_back(id);
    }

    void generateMarketOrder(OrderBook& book, OrderPool& pool) {
        Side side = (uniformDist(rng) < 0.5) ? Side::BUY : Side::SELL;
        Quantity qty = static_cast<Quantity>(sizeDist(rng) * simCfg.avgOrderSize) + 1;
        if (qty > 200) qty = 200;  // Market orders capped smaller

        OrderId id = nextOrderId++;
        book.processOrder(pool.acquire(id, 0, qty, side, true));
    }

    void generateCancel(OrderBook& book) {
        if (outstandingOrders.empty()) return;

        // Pick a random outstanding order to cancel
        size_t idx = static_cast<size_t>(uniformDist(rng) * outstandingOrders.size());
        if (idx >= outstandingOrders.size()) idx = outstandingOrders.size() - 1;

        book.cancelOrder(outstandingOrders[idx]);
        
        // Swap-and-pop removal (O(1))
        outstandingOrders[idx] = outstandingOrders.back();
        outstandingOrders.pop_back();
    }

    // ============================================================
    // EXPORT PNL CURVE TO CSV
    // ============================================================
    void exportPnlCurve(const MarketMaker& mm, const char* filename) {
        const auto& history = mm.getPnlHistory();
        if (history.empty()) return;

        std::ofstream out(filename);
        out << "tick,position,realized_pnl,unrealized_pnl,total_pnl,mid_price\n";

        // Sample every N points to keep file manageable
        size_t step = std::max<size_t>(1, history.size() / 10000);
        for (size_t i = 0; i < history.size(); i += step) {
            const auto& s = history[i];
            out << s.tickNumber << ","
                << s.netPosition << ","
                << std::fixed << std::setprecision(4) << s.realizedPnl << ","
                << s.unrealizedPnl << ","
                << s.totalPnl << ","
                << (static_cast<double>(s.midPrice) / 10000.0) << "\n";
        }
        // Always include the last point
        if (history.size() > 1) {
            const auto& s = history.back();
            out << s.tickNumber << ","
                << s.netPosition << ","
                << std::fixed << std::setprecision(4) << s.realizedPnl << ","
                << s.unrealizedPnl << ","
                << s.totalPnl << ","
                << (static_cast<double>(s.midPrice) / 10000.0) << "\n";
        }
        out.close();
        std::cout << "[SIM] PnL curve exported to " << filename 
                  << " (" << std::min(history.size(), history.size()/step + 1) << " points)\n";
    }
};

// ================================================================
// MAIN
// ================================================================
int main() {
    SimConfig simCfg;
    simCfg.numTicks = 100000;
    simCfg.startPrice = 150.0;
    simCfg.volatility = 0.0003;

    MarketMakerConfig mmCfg;
    mmCfg.halfSpread   = 5000;    // $0.50
    mmCfg.quoteSize    = 50;
    mmCfg.maxPosition  = 500;
    mmCfg.skewPerUnit  = 100;     // $0.01 per share
    mmCfg.maxLoss      = 50000000; // $5,000

    Simulator sim(simCfg, mmCfg, 42);
    sim.run();

    return 0;
}
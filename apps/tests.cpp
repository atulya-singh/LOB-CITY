#include "tests.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <chrono>
#include <string>
#include "../src/core/MemoryPool.h"
#include "../src/core/OrderBook.h"
#include "../src/gateway/OrderEntryGateway.h"
#include "../src/gateway/FixParser.h"
#include "../src/marketdata/UdpPublisher.h"

// Forward-declare to avoid transitive include issues (e.g. missing types.h in
// include path when using certain editor/IDE configurations). The real
// definition is not required for these tests' compilation units here.
class OrderEntryGateway;


void runFunctionalTest(OrderBook& book, OrderPool& pool) {
    std::cout << "--- STARTING FUNCTIONAL TEST (Market Order Sweep) ---\n";
    book.processOrder(pool.acquire(1, 1500000, 50, Side::SELL));  // Ask @ $150.00
    book.processOrder(pool.acquire(2, 1510000, 50, Side::SELL));  // Ask @ $151.00
    book.processOrder(pool.acquire(3, 1520000, 50, Side::SELL));  // Ask @ $152.00
    std::cout << "[+] Liquidity added: 50 units at $150, $151, and $152.\n";

    std::cout << ">>> Executing MARKET BUY for 120 units (should sweep $150, $151, and part of $152)...\n";
    Order* mktOrder = pool.acquire(4, 0, 120, Side::BUY, true); 
    book.processOrder(mktOrder);

    std::cout << "Market Order Remainder: " << mktOrder->quantity << " (Expected: 0)\n";
    std::cout << "--- FUNCTIONAL TEST COMPLETE ---\n\n";
}

void runPipelineTest(OrderEntryGateway& gateway) {
    std::cout << "--- STARTING PIPELINE TEST (FIX -> Gateway -> Engine) ---\n";
    const char* rawNetworkData = "8=FIX.4.2\x01" "35=D\x01" "11=999\x01" "54=1\x01" "38=100\x01" "44=150.50\x01" "40=2\x01";
    size_t dataLength = std::strlen(rawNetworkData); 

    ParsedFixMessage parsedMsg;
    if (parseFixMessage(rawNetworkData, dataLength, parsedMsg)) {
        gateway.onParsedMessage(parsedMsg);
        std::cout << "[+] Successfully parsed raw FIX string and dispatched Order 999 to the matching engine.\n";
    } else {
        std::cout << "[-] Failed to parse FIX message.\n";
    }
    std::cout << "--- PIPELINE TEST COMPLETE ---\n\n";
}

void runBenchmark(OrderBook& book, OrderPool& pool) {
    std::vector<Order*> testOrders;
    for(int i = 0; i < 1000; ++i) {
        bool isMkt = (i % 10 == 0);
        testOrders.push_back(pool.acquire(1000 + i, 1500000, 10, Side::SELL, isMkt));
    }
    std::cout << ">>> Running Latency Benchmark (1,000 mixed orders)...\n";
    auto start = std::chrono::high_resolution_clock::now();
    for (Order* o : testOrders) { book.processOrder(o); }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::cout << "Total time: " << duration << " ns\n";
    std::cout << "Average Latency: " << (duration / 1000.0) << " ns\n";
    std::cout << "Throughput: " << (1000.0 / (duration / 1e9)) << " orders/sec\n\n";
}

void runEndToEndBenchmark(OrderEntryGateway& gateway) {
    std::vector<std::string> fixMessages;
    fixMessages.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        // Alternating buys and sells near $150 — within the 5% price collar
        std::string side = (i % 2 == 0) ? "1" : "2";
        std::string price = (i % 2 == 0) ? "150.00" : "151.00";
        std::string msg = "8=FIX.4.2\x01" "35=D\x01" "11=" + std::to_string(1000 + i) + 
                          "\x01" "54=" + side + "\x01" "38=10\x01" "44=" + price + "\x01" "40=2\x01";
        fixMessages.push_back(msg);
    }
    std::cout << ">>> Running End-to-End Latency Benchmark (1,000 FIX strings -> Parser -> Risk -> Gateway -> Engine)...\n";
    ParsedFixMessage parsedMsg;
    auto start = std::chrono::high_resolution_clock::now();
    for (const auto& msg : fixMessages) {
        if (parseFixMessage(msg.c_str(), msg.length(), parsedMsg)) {
            gateway.onParsedMessage(parsedMsg); 
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::cout << "Total end-to-end time: " << duration << " ns\n";
    std::cout << "Average Pipeline Latency: " << (duration / 1000.0) << " ns per order\n";
    std::cout << "End-to-End Throughput: " << (1000.0 / (duration / 1e9)) << " messages/sec\n\n";
}

// ==========================================
// RISK ENGINE TEST
// Sends a mix of valid and invalid orders through the full pipeline
// to verify that the risk engine correctly blocks dangerous orders.
// ==========================================
void runRiskEngineTest(OrderBook& book, OrderPool& pool, OrderEntryGateway& gateway) {
    std::cout << "--- STARTING RISK ENGINE TEST ---\n";

    // ============================================================
    // SEED THE BOOK DIRECTLY (bypassing risk)
    // In production, initial instrument listings and opening auctions
    // bypass pre-trade risk checks. You can't collar-check against a
    // market that doesn't exist yet. This is the bootstrap problem.
    // ============================================================
    book.processOrder(pool.acquire(8001, 1500000, 100, Side::BUY));   // Bid @ $150.00
    book.processOrder(pool.acquire(8002, 1510000, 100, Side::SELL));  // Ask @ $151.00
    std::cout << "[+] Book seeded directly: BID 100 @ $150.00 | ASK 100 @ $151.00\n";

    auto sendFix = [&](const char* label, const std::string& fixStr) {
        ParsedFixMessage parsedMsg;
        if (parseFixMessage(fixStr.c_str(), fixStr.length(), parsedMsg)) {
            gateway.onParsedMessage(parsedMsg);
            std::cout << "  [" << label << "] Sent order | price=" << parsedMsg.price 
                      << " qty=" << parsedMsg.qty << "\n";
        }
    };

    std::cout << "\n  --- Sending orders that SHOULD PASS ---\n";
    // Normal limit buy at $150.25 — well within the 5% collar
    sendFix("VALID BUY ", "8=FIX.4.2\x01" "35=D\x01" "11=8003\x01" "54=1\x01" "38=50\x01"  "44=150.25\x01" "40=2\x01");
    // Normal limit sell at $150.75
    sendFix("VALID SELL", "8=FIX.4.2\x01" "35=D\x01" "11=8004\x01" "54=2\x01" "38=50\x01"  "44=150.75\x01" "40=2\x01");
    // Market order (no price check, no collar check)
    sendFix("VALID MKT ", "8=FIX.4.2\x01" "35=D\x01" "11=8005\x01" "54=1\x01" "38=10\x01"  "44=0\x01"      "40=1\x01");

    std::cout << "\n  --- Sending orders that SHOULD BE REJECTED ---\n";
    // Fat finger: quantity of 2,000,000 (over the 1,000,000 limit)
    sendFix("FAT FINGER", "8=FIX.4.2\x01" "35=D\x01" "11=8006\x01" "54=1\x01" "38=2000000\x01" "44=150.00\x01" "40=2\x01");
    // Price collar: buy at $200.00 — that's 33% above the $150.50 mid, way beyond the 5% collar
    sendFix("COLLAR BUY", "8=FIX.4.2\x01" "35=D\x01" "11=8007\x01" "54=1\x01" "38=100\x01"    "44=200.00\x01" "40=2\x01");
    // Price collar: sell at $1.00 — way below the market
    sendFix("COLLAR SEL", "8=FIX.4.2\x01" "35=D\x01" "11=8008\x01" "54=2\x01" "38=100\x01"    "44=1.00\x01"   "40=2\x01");
    // Invalid: zero quantity
    sendFix("ZERO QTY  ", "8=FIX.4.2\x01" "35=D\x01" "11=8009\x01" "54=1\x01" "38=0\x01"      "44=150.00\x01" "40=2\x01");

    // Print the risk engine's statistics
    gateway.getRiskEngine().printReport();
    std::cout << "--- RISK ENGINE TEST COMPLETE ---\n\n";
}
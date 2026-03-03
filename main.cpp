#include <iostream>
#include <chrono> 
#include <vector>
#include "OrderBook.h"
#include "MemoryPool.h"

// Note: Ensure you have #define ENABLE_LOGGING in OrderBook.cpp 
// if you want to see the "TRADE" messages for the Functional Test!

void runFunctionalTest(OrderBook& book, OrderPool& pool) {
    std::cout << "--- STARTING FUNCTIONAL TEST (Market Order Sweep) ---\n";

    // 1. Add Sellers at three different price levels
    book.processOrder(pool.acquire(1, 10100, 50, Side::SELL)); // 50 @ $101.00
    book.processOrder(pool.acquire(2, 10200, 50, Side::SELL)); // 50 @ $102.00
    book.processOrder(pool.acquire(3, 10300, 50, Side::SELL)); // 50 @ $103.00
    std::cout << "[+] Liquidity added: 50 units at $101, $102, and $103.\n";

    // 2. Execute a Market Buy that is larger than the first two levels
    // Parameters: ID=4, Price=0 (ignored), Qty=120, Side=BUY, isMarket=true
    std::cout << ">>> Executing MARKET BUY for 120 units (should sweep 101, 102, and part of 103)...\n";
    Order* mktOrder = pool.acquire(4, 0, 120, Side::BUY, true); 
    book.processOrder(mktOrder);

    // 3. Final check: The market order should be fully filled (qty 0) 
    // and the remainder should not be in the book.
    std::cout << "Market Order Remainder: " << mktOrder->quantity << " (Expected: 0)\n";
    std::cout << "--- FUNCTIONAL TEST COMPLETE ---\n\n";
}

void runBenchmark(OrderBook& book, OrderPool& pool) {
    std::vector<Order*> testOrders;
    // Pre-allocate to keep the benchmark pure
    for(int i = 0; i < 1000; ++i) {
        // Mixing it up: 90% Limit Orders, 10% Market Orders
        bool isMkt = (i % 10 == 0);
        testOrders.push_back(pool.acquire(1000 + i, 10100, 10, Side::SELL, isMkt));
    }

    std::cout << ">>> Running Latency Benchmark (1,000 mixed orders)...\n";

    auto start = std::chrono::high_resolution_clock::now();
    for (Order* o : testOrders) {
        book.processOrder(o);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "Total time: " << duration << " ns\n";
    std::cout << "Average Latency: " << (duration / 1000.0) << " ns\n";
    std::cout << "Throughput: " << (1000.0 / (duration / 1e9)) << " orders/sec\n";
}

int main() {
    // Standard setup
    OrderPool pool(20000); 
    OrderBook book(&pool);

    // 1. Prove it works
    runFunctionalTest(book, pool);

    // 2. See how fast it is now
    runBenchmark(book, pool);

    return 0;
}
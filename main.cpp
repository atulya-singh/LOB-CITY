#include <iostream>
#include <chrono> // For high_resolution_clock
#include <vector>
#include "OrderBook.h"
#include "MemoryPool.h"

void benchmark(OrderBook& book, OrderPool& pool) {
    // 1. Prepare a batch of orders to simulate a "burst" of traffic
    // We pre-acquire them so the pool-allocation time doesn't skew our matching results
    std::vector<Order*> testOrders;
    for(int i = 0; i < 1000; ++i) {
        testOrders.push_back(pool.acquire(100 + i, 10100, 10, Side::SELL));
    }

    std::cout << ">>> Running Latency Benchmark (1,000 orders)...\n";

    // 2. Start the clock
    auto start = std::chrono::high_resolution_clock::now();

    for (Order* o : testOrders) {
        book.processOrder(o);
    }

    // 3. Stop the clock
    auto end = std::chrono::high_resolution_clock::now();

    // 4. Calculate duration
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "Total time for 1,000 orders: " << duration << " ns\n";
    std::cout << "Average Latency per order: " << (duration / 1000.0) << " ns\n";
    std::cout << "Throughput: " << (1000.0 / (duration / 1e9)) << " orders/sec\n";
}

int main() {
    OrderPool pool(10000); 
    OrderBook book(&pool);

    // Warm up the CPU cache with a few trades first
    book.processOrder(pool.acquire(1, 10000, 10, Side::BUY));
    book.processOrder(pool.acquire(2, 10000, 10, Side::SELL));

    benchmark(book, pool);

    return 0;
}
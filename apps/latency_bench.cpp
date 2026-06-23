// Latency benchmark — drives ~1M diverse orders through a fresh OrderBook so the
// tail percentiles (p99.9 / p99.99) are measured over a statistically meaningful
// sample. Flow is a realistic mix: limit orders spread across a price band, market
// orders that sweep liquidity, and cancels, on both sides of the book.
//
// Build (from repo root):
//   g++ -O3 -std=c++17 -march=native -pthread \
//       apps/latency_bench.cpp src/gateway/FixParser.cpp src/core/OrderBook.cpp -o latency_bench
//   ./latency_bench
#include <random>
#include <cstdint>
#include "../src/core/types.h"
#include "../src/core/MemoryPool.h"
#include "../src/core/OrderBook.h"

int main() {
    const int N = 1'000'000;

    OrderPool pool(2'000'000);
    UdpPublisher udpPub("239.255.0.1", 3050); // present so publishBBO() pushes to the BBO ring (real hot-path cost)
    OrderBook book(&pool, &udpPub);

    std::mt19937_64 rng(42); // fixed seed → reproducible
    std::uniform_int_distribution<int> tick(-20, 20);  // ±20 ticks around the mid
    std::uniform_int_distribution<int> qty(1, 100);
    std::uniform_int_distribution<int> roll(0, 99);

    const int64_t MID = 1'500'000; // $150.0000 in fixed-point (4 implied decimals)
    OrderId nextId = 1;

    for (int i = 0; i < N; ++i) {
        int r = roll(rng);
        Side side = (i & 1) ? Side::BUY : Side::SELL;

        if (r < 15) {
            // 15% marketable orders — sweep resting liquidity
            book.processOrder(pool.acquire(nextId++, 0, qty(rng), side, true));
        } else if (r < 25) {
            // 10% cancels of a recent id (often a no-op on a filled order — exercises orderMap)
            book.cancelOrder(nextId > 500 ? nextId - (roll(rng) + 1) : 0);
        } else {
            // 75% limit orders at a varied price within the band
            int64_t px = MID + tick(rng) * 100; // 1 tick = $0.01 = 100 fixed-point units
            book.processOrder(pool.acquire(nextId++, px, qty(rng), side, false));
        }
    }

    book.printLatencyReport();
    return 0;
}

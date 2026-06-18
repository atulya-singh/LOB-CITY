#include <iostream>
#include <chrono> 
#include <vector>
#include "OrderBook.h"
#include "MemoryPool.h"
#include "FixParser.h"
#include "OrderEntryGateway.h"
#include "RingBuffer.h" // <-- The Lock-Free Queue
#include "UdpPublisher.h"
#include "LatencyTracker.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <thread>       // <-- Required for multi-threading

// ==========================================
// THE LOCK-FREE SPSC QUEUE
// Capacity must be a power of 2 for maximum compiler optimization
// ==========================================
RingBuffer<ParsedFixMessage, 131072> orderQueue;

// ... [Keep your existing runFunctionalTest, runPipelineTest, runBenchmark, runEndToEndBenchmark exactly the same] ...
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

// ==========================================
// THREAD 2: THE CONSUMER (MATCHING ENGINE)
// ==========================================
void runMatchingEngine(OrderEntryGateway& gateway) {
    std::cout << ">>> [Engine Core] Thread active. Polling lock-free queue...\n";
    ParsedFixMessage msg;
    
    // HFT Concept: The "Busy-Wait" Loop
    // This thread never sleeps. It spins at 100% CPU waiting for data to achieve ultra-low latency.
    while (true) {
        if (orderQueue.pop(msg)) {
            gateway.onParsedMessage(msg);
        }
    }
}

// ==========================================
// THREAD 1: THE PRODUCER (NETWORK I/O)
// Note: We removed the 'gateway' parameter because this thread no longer touches the engine directly.
// ==========================================
void runTCPServer() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) return;

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(5050);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n";
        return;
    }
    if (listen(server_fd, 3) < 0) return;

    while (true) {
        std::cout << ">>> [Network Core] Listening on Port 5050.\n";
        new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (new_socket < 0) continue;

        std::cout << "[+] Client connected!\n";

        std::string accumulator;
        accumulator.reserve(65536);
        char readBuf[4096];          // ✅ only one buffer now

        ParsedFixMessage parsedMsg;
        while (true) {
            ssize_t valread = read(new_socket, readBuf, sizeof(readBuf));  // ✅ read into readBuf

            if (valread <= 0) {
                std::cout << "[-] Client disconnected.\n\n";
                close(new_socket);
                break;
            }

            accumulator.append(readBuf, valread);  // ✅ append readBuf

            while (true) {
                size_t msgEnd = accumulator.find("10=");
                if (msgEnd == std::string::npos) break;

                size_t delimPos = accumulator.find('\x01', msgEnd);
                if (delimPos == std::string::npos) break;

                size_t msgLen = delimPos + 1;

                if (parseFixMessage(accumulator.c_str(), msgLen, parsedMsg)) {  // ✅ parse accumulator
                    while (!orderQueue.push(parsedMsg)) {}
                }
                accumulator.erase(0, msgLen);  // ✅ remove processed message
            }   // ← closes inner while(true)
        }       // ← closes middle while(true) — the read loop
    }           // ← closes outer while(true) — the accept loop
}

int main() {
    OrderPool pool(200000); // Increased pool size for multi-threaded burst
    UdpPublisher udpPub("239.255.0.1", 3050);
    
    // Fixed: Passed udpPub into the OrderBook so the UDP megaphone works!
    OrderBook book(&pool, &udpPub);
    OrderEntryGateway gateway(&pool, &book);

    // Run Benchmarks (Synchronous)
    runFunctionalTest(book, pool);
    runPipelineTest(gateway);
    runRiskEngineTest(book, pool, gateway);
    runBenchmark(book, pool);
    runEndToEndBenchmark(gateway);
    book.printLatencyReport();
    book.display();
    book.printMarketStats();
    gateway.getRiskEngine().printReport();

    std::cout << std::flush; // Flush all test output before entering multi-threaded mode

    // --- START MULTI-THREADING ---
    // Spin up Thread 2 (The Matching Engine) in the background
    std::thread engineThread(runMatchingEngine, std::ref(gateway));

    // Keep Thread 1 (Network) running on the main execution path
    runTCPServer();

    // Rejoin the thread on shutdown (though the server loop runs forever)
    engineThread.join();

    return 0;
}
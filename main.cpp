#include <iostream>
#include <chrono> 
#include <vector>
#include "OrderBook.h"
#include "MemoryPool.h"
#include "FixParser.h"
#include "OrderEntryGateway.h"
#include "RingBuffer.h" // <-- The Lock-Free Queue
#include "UdpPublisher.h"
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
    book.processOrder(pool.acquire(1, 10100, 50, Side::SELL)); 
    book.processOrder(pool.acquire(2, 10200, 50, Side::SELL)); 
    book.processOrder(pool.acquire(3, 10300, 50, Side::SELL)); 
    std::cout << "[+] Liquidity added: 50 units at $101, $102, and $103.\n";

    std::cout << ">>> Executing MARKET BUY for 120 units (should sweep 101, 102, and part of 103)...\n";
    Order* mktOrder = pool.acquire(4, 0, 120, Side::BUY, true); 
    book.processOrder(mktOrder);

    std::cout << "Market Order Remainder: " << mktOrder->quantity << " (Expected: 0)\n";
    std::cout << "--- FUNCTIONAL TEST COMPLETE ---\n\n";
}

void runPipelineTest(OrderEntryGateway& gateway) {
    std::cout << "--- STARTING PIPELINE TEST (FIX -> Gateway -> Engine) ---\n";
    const char* rawNetworkData = "8=FIX.4.2\x01" "35=D\x01" "11=999\x01" "54=1\x01" "38=100\x01" "44=150.5\x01" "40=2\x01";
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
        testOrders.push_back(pool.acquire(1000 + i, 10100, 10, Side::SELL, isMkt));
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
        std::string msg = "8=FIX.4.2\x01" "35=D\x01" "11=" + std::to_string(1000 + i) + 
                          "\x01" "54=2\x01" "38=10\x01" "44=101.5\x01" "40=2\x01";
        fixMessages.push_back(msg);
    }
    std::cout << ">>> Running End-to-End Latency Benchmark (1,000 FIX strings -> Parser -> Gateway -> Engine)...\n";
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
    char buffer[1024] = {0}; 

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
        std::cout << ">>> [Network Core] Listening on Port 5050. Waiting for Market Replayer...\n";
        new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (new_socket < 0) continue;

        std::cout << "[+] Client connected! Pushing incoming orders to Ring Buffer.\n";

        ParsedFixMessage parsedMsg;
        while (true) {
            ssize_t valread = read(new_socket, buffer, sizeof(buffer)-1);

            if (valread <= 0) {
                std::cout << "[-] Client disconnected. Resetting for next connection...\n\n";
                close(new_socket); 
                break; 
            }
            buffer[valread] = '\0'; 
            
            if (parseFixMessage(buffer, valread, parsedMsg)) {
                // DROP DATA INTO QUEUE
                // If the queue is full (engine is lagging behind network), spin and wait
                while (!orderQueue.push(parsedMsg)) {
                    // Backpressure handling
                }
            }
        }
    }
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
    runBenchmark(book, pool);
    runEndToEndBenchmark(gateway);

    // --- START MULTI-THREADING ---
    // Spin up Thread 2 (The Matching Engine) in the background
    std::thread engineThread(runMatchingEngine, std::ref(gateway));

    // Keep Thread 1 (Network) running on the main execution path
    runTCPServer();

    // Rejoin the thread on shutdown (though the server loop runs forever)
    engineThread.join();

    return 0;
}
#include <iostream>
#include <chrono> 
#include <vector>
#include "../src/core/OrderBook.h"
#include "../src/core/MemoryPool.h"
#include "../src/gateway/FixParser.h"
#include "../src/gateway/OrderEntryGateway.h"
#include "../src/core/RingBuffer.h" 
#include "../src/marketdata/UdpPublisher.h"
#include "../src/core/LatencyTracker.h"
#include "../src/marketdata/RingBufferBBO.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <thread> 
#include "tests.h"   

// ==========================================
// THE LOCK-FREE SPSC QUEUE
// ==========================================
RingBuffer<ParsedFixMessage, 131072> orderQueue;
extern RingBufferBBO buffer; // defined in OrderBook.cpp
// ==========================================
// THREAD 2: THE CONSUMER (MATCHING ENGINE)
// ==========================================
void runMatchingEngine(OrderEntryGateway& gateway) {
    std::cout << ">>> [Engine Core] Thread active. Polling lock-free queue...\n";
    ParsedFixMessage msg;
    
    
    while (true) {
        if (orderQueue.pop(msg)) {
            gateway.onParsedMessage(msg);
        }
    }
}
// Thread 3 for BBO 
void DisplayBBO(UdpPublisher& udp){
    
    while (true){
        auto result = buffer.pop();
        if(result) {
            BboMessage msg = *result;
            
            udp.publishBbo(msg);
        }
    }
}
// ==========================================
// THREAD 1: THE PRODUCER (NETWORK I/O)
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
        char readBuf[4096];          

        ParsedFixMessage parsedMsg;
        while (true) {
            ssize_t valread = read(new_socket, readBuf, sizeof(readBuf));  

            if (valread <= 0) {
                std::cout << "[-] Client disconnected.\n\n";
                close(new_socket);
                break;
            }

            accumulator.append(readBuf, valread);  

            while (true) {
                size_t msgEnd = accumulator.find("10=");
                if (msgEnd == std::string::npos) break;

                size_t delimPos = accumulator.find('\x01', msgEnd);
                if (delimPos == std::string::npos) break;

                size_t msgLen = delimPos + 1;

                if (parseFixMessage(accumulator.c_str(), msgLen, parsedMsg)) {  
                    while (!orderQueue.push(parsedMsg)) {}
                }
                accumulator.erase(0, msgLen);  
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
    std::thread thirdThread(DisplayBBO, std::ref(udpPub));
    std::thread engineThread(runMatchingEngine, std::ref(gateway));

    // Keep Thread 1 (Network) running on the main execution path
    runTCPServer();

    // Rejoin the thread on shutdown (though the server loop runs forever)
    engineThread.join();

    return 0;
}
#include <iostream>
#include <chrono> 
#include <vector>
#include "OrderBook.h"
#include "MemoryPool.h"
#include "FixParser.h"
#include "OrderEntryGateway.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>


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

void runPipelineTest(OrderEntryGateway& gateway) {
    std::cout << "--- STARTING PIPELINE TEST (FIX -> Gateway -> Engine) ---\n";
    
    // Simulate an incoming raw FIX message from the network: 
    // New Order Single (35=D), ID 999 (11=999), Buy (54=1), Qty 100 (38=100), Price 150.5 (44=150.5), Limit (40=2)
    const char* rawNetworkData = "8=FIX.4.2\x01" "35=D\x01" "11=999\x01" "54=1\x01" "38=100\x01" "44=150.5\x01" "40=2\x01";
    size_t dataLength = std::strlen(rawNetworkData); // Approximate length of the string above

    ParsedFixMessage parsedMsg;
    
    // Test the zero-allocation pipeline
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
void runEndToEndBenchmark(OrderEntryGateway& gateway) {
    std::vector<std::string> fixMessages;
    fixMessages.reserve(1000);

    // Pre-generate 1,000 raw FIX messages
    for (int i = 0; i < 1000; ++i) {
        // Incrementing Tag 11 (ClOrdID) so each order is unique
        // Setting Tag 54=2 (Sell), Tag 38=10 (Qty), Tag 44=101.5 (Price), Tag 40=2 (Limit)
        std::string msg = "8=FIX.4.2\x01" "35=D\x01" "11=" + std::to_string(1000 + i) + 
                          "\x01" "54=2\x01" "38=10\x01" "44=101.5\x01" "40=2\x01";
        fixMessages.push_back(msg);
    }

    std::cout << ">>> Running End-to-End Latency Benchmark (1,000 FIX strings -> Parser -> Gateway -> Engine)...\n";

    ParsedFixMessage parsedMsg;
    
    // START THE CLOCK
    auto start = std::chrono::high_resolution_clock::now();
    
    for (const auto& msg : fixMessages) {
        // 1. Parse the string
        if (parseFixMessage(msg.c_str(), msg.length(), parsedMsg)) {
            // 2. Route through Gateway (allocates from pool + maps data)
            // 3. Gateway dispatches to the OrderBook
            gateway.onParsedMessage(parsedMsg); 
        }
    }
    
    // STOP THE CLOCK
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "Total end-to-end time: " << duration << " ns\n";
    std::cout << "Average Pipeline Latency: " << (duration / 1000.0) << " ns per order\n";
    std::cout << "End-to-End Throughput: " << (1000.0 / (duration / 1e9)) << " messages/sec\n\n";
}

void runTCPServer(OrderEntryGateway& gateway) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    char buffer[1024] = {0}; // Buffer to hold incoming network bytes

    // 1. Create the main server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        std::cerr << "Socket creation failed\n";
        return;
    }

    // 2. Configure socket to reuse the port
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listens on LOCAL IP
    address.sin_port = htons(5050);       // Port 5050

    // 3. Bind the socket to the port
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n";
        return;
    }
    
    // 4. Start listening for incoming connections
    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed\n";
        return;
    }

    // 5. THE OUTER LOOP: Keep the server alive forever
    while (true) {
        std::cout << "\n>>> Server is listening on Port 5050. Waiting for Market Replayer...\n";

        // 6. ACCEPT: This blocks and waits until a client actually connects
        new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (new_socket < 0) {
            std::cerr << "Accept failed\n";
            continue; // If it fails, go back to the top and wait again
        }

        std::cout << "[+] Client connected! Ready to receive orders.\n";

        ParsedFixMessage parsedMsg;
        
        // 7. THE INNER LOOP: Read bytes continuously until this specific client hangs up
        while (true) {
            ssize_t valread = read(new_socket, buffer, sizeof(buffer)-1);

            if (valread <= 0) {
                std::cout << "[-] Client disconnected. Resetting for next connection...\n";
                close(new_socket); // Hang up the client's dedicated line
                break; // Break out of the inner loop to go back to accept()
            }
            
            buffer[valread] = '\0'; // Safety null-termination
            
            // 8. Feed the network bytes to your parser and matching engine
            if (parseFixMessage(buffer, valread, parsedMsg)) {
                gateway.onParsedMessage(parsedMsg);
                
                // Note: Printing to the console is very slow! 
                // Comment this line out when you want to measure maximum throughput.
                // std::cout << "Processed Order ID : " << parsedMsg.clOrdID << "\n";
            }
        }
    }
}


int main() {
    // Standard setup
    OrderPool pool(20000); 
    OrderBook book(&pool);
    OrderEntryGateway gateway(&pool, &book);

    // 1. Prove the core engine works
    runFunctionalTest(book, pool);

    // 2. Prove the FIX-to-Engine pipeline works
    runPipelineTest(gateway);

    // 3. See how fast the engine is
    runBenchmark(book, pool);

    runEndToEndBenchmark(gateway);

    runTCPServer(gateway);

    return 0;
}
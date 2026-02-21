#include <iostream>
#include "OrderBook.h"
#include "MemoryPool.h" // Include our new pool!

int main() {
    // 1. Boot up the Memory Pool before the market opens
    // We pre-allocate 1,000 orders so we never talk to the OS during trading.
    OrderPool pool(1000); 
    
    OrderBook book;
    std::cout << "=== HFT Engine Started (Zero-Allocation Mode) ===\n\n";

    // 2. Build the Ask Side using pool.acquire() instead of 'new'
    Order* sell1 = pool.acquire(1, 10100, 50, Side::SELL);  
    Order* sell2 = pool.acquire(2, 10100, 50, Side::SELL);  
    Order* sell3 = pool.acquire(3, 10200, 100, Side::SELL); 

    book.processOrder(sell1);
    book.processOrder(sell2);
    book.processOrder(sell3);
    std::cout << "[+] Added 3 Sellers. Best Ask is $101.00\n";

    // 3. Build the Bid Side
    Order* buy1 = pool.acquire(4, 9900, 100, Side::BUY); 
    Order* buy2 = pool.acquire(5, 9800, 50, Side::BUY);  

    book.processOrder(buy1);
    book.processOrder(buy2);
    std::cout << "[+] Added 2 Buyers. Best Bid is $99.00\n";
    
    std::cout << "\nCurrent Market Spread: $99.00 Bid // $101.00 Ask\n";
    std::cout << "------------------------------------------------\n\n";

    // 4. The Aggressor
    std::cout << ">>> INCOMING AGGRESSIVE ORDER: BUY 120 shares @ $105.00 <<<\n\n";
    
    Order* aggressiveBuy = pool.acquire(6, 10500, 120, Side::BUY);
    book.processOrder(aggressiveBuy);

    std::cout << "\n------------------------------------------------\n";
    std::cout << "Aggressive order finished processing.\n";
    std::cout << "Unfilled remainder: " << aggressiveBuy->quantity << " shares.\n\n";

    // 5. Look, ma! No delete calls! 
    // The OrderPool's destructor will handle cleaning up the memory automatically 
    // when the program ends.

    std::cout << "=== Engine Shutting Down ===\n";
    return 0;
}
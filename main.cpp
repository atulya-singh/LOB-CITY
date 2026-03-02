#include <iostream>
#include "OrderBook.h"
#include "MemoryPool.h"

int main() {
    OrderPool pool(1000); 
    OrderBook book(&pool);

    std::cout << "=== HFT Engine Started ===\n";

    // Add initial liquidity
    book.processOrder(pool.acquire(1, 10100, 50, Side::SELL));  
    book.processOrder(pool.acquire(2, 10100, 50, Side::SELL));  
    book.processOrder(pool.acquire(3, 10200, 100, Side::SELL)); 
    book.processOrder(pool.acquire(4, 9900, 100, Side::BUY)); 

    book.display();

    std::cout << ">>> INCOMING AGGRESSIVE BUY: 120 shares @ $105.00\n";
    Order* agg = pool.acquire(6, 10500, 120, Side::BUY);
    book.processOrder(agg);

    book.display();

    std::cout << "=== Engine Shutting Down ===\n";
    return 0;
}
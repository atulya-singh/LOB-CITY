#include <iostream>
#include "OrderBook.h"

int main() {
    OrderBook book;
    std::cout << "--- Market is Open ---\n\n";

    // 1. Build the Ask Side (Sellers)
    // Remember: Price is in ticks. 10100 = $101.00
    Order* sell1 = new Order(1, 10100, 50, Side::SELL);   // $101.00
    Order* sell2 = new Order(2, 10100, 50, Side::SELL);  // $101.00
    Order* sell3 = new Order(3, 10200, 100, Side::SELL); // $102.00

    book.processOrder(sell1);
    book.processOrder(sell2);
    book.processOrder(sell3);
    std::cout << "[+] Added 3 Sellers. Best Ask is $101.00\n";

    // 2. Build the Bid Side (Buyers)
    Order* buy1 = new Order(4, 9900, 100, Side::BUY); // $99.00
    Order* buy2 = new Order(5, 9800, 50, Side::BUY);  // $98.00

    book.processOrder(buy1);
    book.processOrder(buy2);
    std::cout << "[+] Added 2 Buyers. Best Bid is $99.00\n";
    
    std::cout << "\nCurrent Spread: $99.00 Bid // $101.00 Ask\n";
    std::cout << "------------------------------------------\n\n";

    // 3. The Aggressor
    // Someone really wants to buy right now. They want 120 shares, 
    // and they are willing to pay up to $105.00 to get them.
    std::cout << ">>> INCOMING AGGRESSIVE ORDER: BUY 120 shares @ $105.00 <<<\n\n";
    
    Order* aggressiveBuy = new Order(6, 10500, 120, Side::BUY);
    book.processOrder(aggressiveBuy);

    std::cout << "\n------------------------------------------\n";
    std::cout << "Aggressive order finished matching.\n";
    std::cout << "Remaining unfilled quantity: " << aggressiveBuy->quantity << " shares.\n";

    // Cleanup (We will automate this with a Memory Pool next)
    delete sell1; delete sell2; delete sell3;
    delete buy1; delete buy2; delete aggressiveBuy;

    return 0;
}
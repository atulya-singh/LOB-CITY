#include <iostream>
#include "OrderBook.h"

// A quick helper function to print the current state of the queue
void printQueue(PriceLevel& level) {
    std::cout << "Price Level: " << level.price 
              << " | Total Volume: " << level.totalVolume << "\n";
    
    Order* current = level.head;
    int count = 1;
    while (current != nullptr) {
        std::cout << "  Position " << count << " -> OrderID: " << current->id 
                  << " | Qty: " << current->quantity << "\n";
        current = current->next;
        count++;
    }
    std::cout << "-----------------------------------\n";
}

int main() {
    std::cout << "Starting Order Book Test...\n\n";

    // 1. Create a Price Level for $150.00 (represented as 15000 ticks)
    PriceLevel level150(15000);

    // 2. Create three dummy orders
    // Note: We are using 'new' here just for the test. 
    // In the real engine, we will pull these from a pre-allocated Memory Pool!
    Order* order1 = new Order(101, 15000, 100, Side::BUY);
    Order* order2 = new Order(102, 15000, 50,  Side::BUY);
    Order* order3 = new Order(103, 15000, 200, Side::BUY);

    // 3. Add them to the price level queue
    std::cout << "Adding 3 orders...\n";
    level150.appendOrder(order1);
    level150.appendOrder(order2);
    level150.appendOrder(order3);

    printQueue(level150);

    // 4. The tricky part: Cancel the MIDDLE order (OrderID 102)
    std::cout << "Canceling middle order (OrderID 102)...\n";
    
    level150.removeOrder(order2);

    printQueue(level150);

    // 5. Clean up memory (Again, our future Memory Pool will handle this automatically)
    delete order1;
    delete order2;
    delete order3;

    return 0;
}
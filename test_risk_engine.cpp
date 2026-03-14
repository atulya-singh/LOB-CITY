#include <iostream>
#include <cassert>
#include <cstring>
#include "types.h"
#include "MemoryPool.h"
#include "OrderBook.h"
#include "OrderEntryGateway.h"
#include "FixParser.h"
#include "RiskEngine.h"

// ================================================================
// TEST FRAMEWORK
// ================================================================

static int testsRun = 0;
static int testsPassed = 0;

#define RUN_TEST(fn) do { \
    testsRun++; \
    std::cout << "  [RUN ] " << #fn << "..."; \
    fn(); \
    testsPassed++; \
    std::cout << " PASSED\n"; \
} while(0)

// Helper: parse a raw FIX string
ParsedFixMessage parseFix(const char* fixStr) {
    ParsedFixMessage msg;
    parseFixMessage(fixStr, std::strlen(fixStr), msg);
    return msg;
}

// ================================================================
// TEST GROUP 1: BASIC ORDER MATCHING
// ================================================================

void test_limit_buy_matches_resting_sell() {
    OrderPool pool(100);
    OrderBook book(&pool);

    // Resting sell @ $150 for 100 shares
    book.processOrder(pool.acquire(1, 1500000, 100, Side::SELL));
    // Incoming buy @ $150 for 50 shares — should partially fill
    book.processOrder(pool.acquire(2, 1500000, 50, Side::BUY));

    assert(book.getTradeLog().size() == 1);
    assert(book.getTradeLog()[0].quantity == 50);
    assert(book.getTradeLog()[0].price == 1500000);
    assert(book.getTradeLog()[0].buyOrderId == 2);
    assert(book.getTradeLog()[0].sellOrderId == 1);
}

void test_limit_sell_matches_resting_buy() {
    OrderPool pool(100);
    OrderBook book(&pool);

    book.processOrder(pool.acquire(1, 1500000, 100, Side::BUY));
    book.processOrder(pool.acquire(2, 1500000, 50, Side::SELL));

    assert(book.getTradeLog().size() == 1);
    assert(book.getTradeLog()[0].quantity == 50);
    assert(book.getTradeLog()[0].buyOrderId == 1);
    assert(book.getTradeLog()[0].sellOrderId == 2);
}

void test_no_match_when_prices_dont_cross() {
    OrderPool pool(100);
    OrderBook book(&pool);

    // Bid @ $149, Ask @ $151 — no crossing, both rest on the book
    book.processOrder(pool.acquire(1, 1490000, 100, Side::BUY));
    book.processOrder(pool.acquire(2, 1510000, 100, Side::SELL));

    assert(book.getTradeLog().empty());
    assert(book.getBestBid() == 1490000);
    assert(book.getBestAsk() == 1510000);
}

void test_exact_fill_removes_both_orders() {
    OrderPool pool(100);
    OrderBook book(&pool);

    book.processOrder(pool.acquire(1, 1500000, 100, Side::SELL));
    book.processOrder(pool.acquire(2, 1500000, 100, Side::BUY));

    assert(book.getTradeLog().size() == 1);
    assert(book.getTradeLog()[0].quantity == 100);
    // Book should be empty — both orders fully consumed
    assert(book.getBestBid() == 0);
    assert(book.getBestAsk() == 0);
}

// ================================================================
// TEST GROUP 2: MARKET ORDERS
// ================================================================

void test_market_buy_sweeps_multiple_levels() {
    OrderPool pool(100);
    OrderBook book(&pool);

    // Three ask levels: 50 @ $150, 50 @ $151, 50 @ $152
    book.processOrder(pool.acquire(1, 1500000, 50, Side::SELL));
    book.processOrder(pool.acquire(2, 1510000, 50, Side::SELL));
    book.processOrder(pool.acquire(3, 1520000, 50, Side::SELL));

    // Market buy for 120 — should sweep all of $150, all of $151, and 20 of $152
    book.processOrder(pool.acquire(4, 0, 120, Side::BUY, true));

    assert(book.getTradeLog().size() == 3);
    assert(book.getTradeLog()[0].quantity == 50);  // filled 50 @ $150
    assert(book.getTradeLog()[0].price == 1500000);
    assert(book.getTradeLog()[1].quantity == 50);  // filled 50 @ $151
    assert(book.getTradeLog()[1].price == 1510000);
    assert(book.getTradeLog()[2].quantity == 20);  // filled 20 @ $152
    assert(book.getTradeLog()[2].price == 1520000);

    // 30 shares should remain at $152
    assert(book.getBestAsk() == 1520000);
}

void test_market_sell_sweeps_bids() {
    OrderPool pool(100);
    OrderBook book(&pool);

    book.processOrder(pool.acquire(1, 1520000, 50, Side::BUY));
    book.processOrder(pool.acquire(2, 1510000, 50, Side::BUY));

    // Market sell for 75 — sweep all of $152 bid, 25 of $151 bid
    book.processOrder(pool.acquire(3, 0, 75, Side::SELL, true));

    assert(book.getTradeLog().size() == 2);
    assert(book.getTradeLog()[0].quantity == 50);
    assert(book.getTradeLog()[0].price == 1520000);
    assert(book.getTradeLog()[1].quantity == 25);
    assert(book.getTradeLog()[1].price == 1510000);
}

void test_market_order_with_no_liquidity() {
    OrderPool pool(100);
    OrderBook book(&pool);

    // Market buy into an empty book — nothing to fill, order should be released
    Order* mkt = pool.acquire(1, 0, 100, Side::BUY, true);
    book.processOrder(mkt);

    assert(book.getTradeLog().empty());
    // Book stays empty — unfilled market orders don't rest
    assert(book.getBestBid() == 0);
    assert(book.getBestAsk() == 0);
}

// ================================================================
// TEST GROUP 3: PRICE-TIME PRIORITY
// ================================================================

void test_time_priority_fifo_at_same_price() {
    OrderPool pool(100);
    OrderBook book(&pool);

    // Three sells at the same price — order 1 arrived first
    book.processOrder(pool.acquire(1, 1500000, 30, Side::SELL));
    book.processOrder(pool.acquire(2, 1500000, 30, Side::SELL));
    book.processOrder(pool.acquire(3, 1500000, 30, Side::SELL));

    // Buy 50 — should fill order 1 (30) then order 2 (20 of 30)
    book.processOrder(pool.acquire(4, 1500000, 50, Side::BUY));

    assert(book.getTradeLog().size() == 2);
    assert(book.getTradeLog()[0].sellOrderId == 1);  // First in line
    assert(book.getTradeLog()[0].quantity == 30);
    assert(book.getTradeLog()[1].sellOrderId == 2);  // Second in line
    assert(book.getTradeLog()[1].quantity == 20);
}

void test_price_priority_better_price_fills_first() {
    OrderPool pool(100);
    OrderBook book(&pool);

    // Sell @ $152 arrives first, then sell @ $150 arrives second
    book.processOrder(pool.acquire(1, 1520000, 50, Side::SELL));
    book.processOrder(pool.acquire(2, 1500000, 50, Side::SELL));

    // Market buy — should fill the $150 ask first (better price) even 
    // though the $152 ask arrived earlier
    book.processOrder(pool.acquire(3, 0, 30, Side::BUY, true));

    assert(book.getTradeLog().size() == 1);
    assert(book.getTradeLog()[0].sellOrderId == 2);  // $150 fills first
    assert(book.getTradeLog()[0].price == 1500000);
}

// ================================================================
// TEST GROUP 4: CANCEL ORDERS
// ================================================================

void test_cancel_removes_order_from_book() {
    OrderPool pool(100);
    OrderBook book(&pool);

    book.processOrder(pool.acquire(1, 1500000, 100, Side::SELL));
    assert(book.getBestAsk() == 1500000);

    book.cancelOrder(1);
    assert(book.getBestAsk() == 0);  // Book is empty
}

void test_cancel_nonexistent_order_is_noop() {
    OrderPool pool(100);
    OrderBook book(&pool);

    book.processOrder(pool.acquire(1, 1500000, 100, Side::SELL));
    book.cancelOrder(999);  // Doesn't exist — should not crash
    assert(book.getBestAsk() == 1500000);  // Original order still there
}

void test_cancel_middle_order_preserves_queue() {
    OrderPool pool(100);
    OrderBook book(&pool);

    // Three orders at same price: 1, 2, 3
    book.processOrder(pool.acquire(1, 1500000, 30, Side::SELL));
    book.processOrder(pool.acquire(2, 1500000, 30, Side::SELL));
    book.processOrder(pool.acquire(3, 1500000, 30, Side::SELL));

    // Cancel the middle one
    book.cancelOrder(2);

    // Buy 50 — should fill order 1 (30), then order 3 (20 of 30)
    // Order 2 is gone. Order 3 kept its position after order 1.
    book.processOrder(pool.acquire(4, 1500000, 50, Side::BUY));

    assert(book.getTradeLog().size() == 2);
    assert(book.getTradeLog()[0].sellOrderId == 1);
    assert(book.getTradeLog()[0].quantity == 30);
    assert(book.getTradeLog()[1].sellOrderId == 3);  // NOT 2
    assert(book.getTradeLog()[1].quantity == 20);
}

// ================================================================
// TEST GROUP 5: MODIFY ORDERS (Cancel/Replace)
// ================================================================

void test_modify_qty_down_keeps_priority() {
    // This is the key behavioral test. If you reduce quantity at the
    // same price, you keep your position in the queue.
    OrderPool pool(100);
    OrderBook book(&pool);

    // Order 1 and 2 at $150. Order 1 has time priority.
    book.processOrder(pool.acquire(1, 1500000, 100, Side::SELL));
    book.processOrder(pool.acquire(2, 1500000, 100, Side::SELL));

    // Modify order 1: reduce qty from 100 to 30. Same price → keeps priority.
    book.modifyOrder(1, 10, 1500000, 30);

    // Buy 40 — should fill order 10 (was order 1, 30 shares), then 10 of order 2
    book.processOrder(pool.acquire(3, 1500000, 40, Side::BUY));

    assert(book.getTradeLog().size() == 2);
    assert(book.getTradeLog()[0].sellOrderId == 10);  // Modified order fills first
    assert(book.getTradeLog()[0].quantity == 30);
    assert(book.getTradeLog()[1].sellOrderId == 2);   // Original order 2 fills second
    assert(book.getTradeLog()[1].quantity == 10);
}

void test_modify_qty_up_loses_priority() {
    // Increasing quantity is aggressive — you lose your position.
    OrderPool pool(100);
    OrderBook book(&pool);

    book.processOrder(pool.acquire(1, 1500000, 50, Side::SELL));
    book.processOrder(pool.acquire(2, 1500000, 50, Side::SELL));

    // Modify order 1: increase qty from 50 to 80. Same price but qty UP → loses priority.
    book.modifyOrder(1, 10, 1500000, 80);

    // Buy 60 — should fill order 2 first (it now has priority), then 10 of order 10
    book.processOrder(pool.acquire(3, 1500000, 60, Side::BUY));

    assert(book.getTradeLog().size() == 2);
    assert(book.getTradeLog()[0].sellOrderId == 2);   // Order 2 now has priority
    assert(book.getTradeLog()[0].quantity == 50);
    assert(book.getTradeLog()[1].sellOrderId == 10);  // Modified order is at the back
    assert(book.getTradeLog()[1].quantity == 10);
}

void test_modify_price_loses_priority() {
    // Any price change = lose priority, even if the new price is "worse"
    OrderPool pool(100);
    OrderBook book(&pool);

    book.processOrder(pool.acquire(1, 1500000, 50, Side::SELL));
    book.processOrder(pool.acquire(2, 1500000, 50, Side::SELL));

    // Modify order 1: change price to $150.01 (slightly worse for a sell)
    // Price changed → lose priority and move to the new price level
    book.modifyOrder(1, 10, 1500100, 50);

    // Buy 60 @ $151 — should fill all of order 2 at $150.00 first,
    // then 10 of order 10 at $150.01
    book.processOrder(pool.acquire(3, 1510000, 60, Side::BUY));

    assert(book.getTradeLog().size() == 2);
    assert(book.getTradeLog()[0].sellOrderId == 2);
    assert(book.getTradeLog()[0].price == 1500000);
    assert(book.getTradeLog()[1].sellOrderId == 10);
    assert(book.getTradeLog()[1].price == 1500100);
}

void test_modify_to_better_price_moves_level() {
    // Improve the price — order moves to a new, more aggressive level
    OrderPool pool(100);
    OrderBook book(&pool);

    // Sell @ $152
    book.processOrder(pool.acquire(1, 1520000, 50, Side::SELL));
    // Sell @ $151
    book.processOrder(pool.acquire(2, 1510000, 50, Side::SELL));

    assert(book.getBestAsk() == 1510000);  // Best ask is $151

    // Modify order 1: move price from $152 to $150 (now the best ask)
    book.modifyOrder(1, 10, 1500000, 50);

    assert(book.getBestAsk() == 1500000);  // New best ask is $150
}

void test_modify_nonexistent_order_is_noop() {
    OrderPool pool(100);
    OrderBook book(&pool);

    book.processOrder(pool.acquire(1, 1500000, 100, Side::SELL));

    // Modify order 999 which doesn't exist — should not crash
    book.modifyOrder(999, 1000, 1510000, 50);

    // Book should be unchanged
    assert(book.getBestAsk() == 1500000);
    assert(book.getTradeLog().empty());
}

void test_modify_updates_order_id_in_map() {
    // After modify, the old ID should be gone and the new ID should work
    OrderPool pool(100);
    OrderBook book(&pool);

    book.processOrder(pool.acquire(1, 1500000, 100, Side::SELL));

    // Modify: order 1 → order 10
    book.modifyOrder(1, 10, 1500000, 80);

    // Cancel by old ID should fail (it's gone)
    book.cancelOrder(1);
    assert(book.getBestAsk() == 1500000);  // Still there

    // Cancel by new ID should work
    book.cancelOrder(10);
    assert(book.getBestAsk() == 0);  // Now it's gone
}

void test_modify_bid_side() {
    // Verify modify works on the bid side too, not just asks
    OrderPool pool(100);
    OrderBook book(&pool);

    book.processOrder(pool.acquire(1, 1500000, 100, Side::BUY));
    book.processOrder(pool.acquire(2, 1500000, 100, Side::BUY));

    // Modify order 1: reduce qty, keep priority
    book.modifyOrder(1, 10, 1500000, 30);

    // Sell 40 — should fill order 10 (30) then order 2 (10)
    book.processOrder(pool.acquire(3, 1500000, 40, Side::SELL));

    assert(book.getTradeLog().size() == 2);
    assert(book.getTradeLog()[0].buyOrderId == 10);
    assert(book.getTradeLog()[0].quantity == 30);
    assert(book.getTradeLog()[1].buyOrderId == 2);
    assert(book.getTradeLog()[1].quantity == 10);
}

// ================================================================
// TEST GROUP 6: MODIFY THROUGH THE FULL FIX PIPELINE
// ================================================================

void test_fix_cancel_replace_message() {
    OrderPool pool(100);
    OrderBook book(&pool);
    OrderEntryGateway gateway(&pool, &book);

    // Seed the book directly so risk collar has a reference
    book.processOrder(pool.acquire(1, 1500000, 100, Side::SELL));
    book.processOrder(pool.acquire(2, 1490000, 100, Side::BUY));

    // Send a new order via FIX
    auto newOrder = parseFix(
        "8=FIX.4.2\x01" "35=D\x01" "11=100\x01" "54=2\x01" 
        "38=200\x01" "44=151.00\x01" "40=2\x01"
    );
    gateway.onParsedMessage(newOrder);

    // Modify it: change qty from 200 to 80, same price
    // Tag 41=100 (original), Tag 11=101 (new ID)
    auto modMsg = parseFix(
        "8=FIX.4.2\x01" "35=G\x01" "41=100\x01" "11=101\x01" 
        "54=2\x01" "38=80\x01" "44=151.00\x01" "40=2\x01"
    );
    gateway.onParsedMessage(modMsg);

    // Buy into the modified order — should only get 80 shares
    book.processOrder(pool.acquire(3, 1510000, 200, Side::BUY));

    // Find the trade against the modified order (ID 101)
    bool foundModifiedTrade = false;
    for (const auto& t : book.getTradeLog()) {
        if (t.sellOrderId == 101) {
            assert(t.quantity == 80);  // Modified qty, not original 200
            foundModifiedTrade = true;
        }
    }
    assert(foundModifiedTrade);
}

void test_fix_cancel_replace_rejected_by_risk() {
    OrderPool pool(100);
    OrderBook book(&pool);
    OrderEntryGateway gateway(&pool, &book);

    // Seed the book
    book.processOrder(pool.acquire(1, 1500000, 100, Side::SELL));
    book.processOrder(pool.acquire(2, 1490000, 100, Side::BUY));

    // Send a normal order via FIX
    auto newOrder = parseFix(
        "8=FIX.4.2\x01" "35=D\x01" "11=100\x01" "54=2\x01" 
        "38=50\x01" "44=151.00\x01" "40=2\x01"
    );
    gateway.onParsedMessage(newOrder);

    // Try to modify it to $999 — should be collar-rejected
    auto modMsg = parseFix(
        "8=FIX.4.2\x01" "35=G\x01" "41=100\x01" "11=101\x01" 
        "54=2\x01" "38=50\x01" "44=999.00\x01" "40=2\x01"
    );
    gateway.onParsedMessage(modMsg);

    // The original order should still be on the book with its original values.
    // Buy into it — should get 50 shares at $151 (the original order, unmodified)
    book.processOrder(pool.acquire(3, 1510000, 200, Side::BUY));

    bool foundOriginal = false;
    for (const auto& t : book.getTradeLog()) {
        if (t.sellOrderId == 100) {
            assert(t.quantity == 50);
            assert(t.price == 1510000);
            foundOriginal = true;
        }
    }
    assert(foundOriginal);
}

// ================================================================
// TEST GROUP 7: EDGE CASES
// ================================================================

void test_self_trade_at_same_price() {
    // An incoming order that matches against resting orders — even if
    // both sides are from the "same" client (we don't track client IDs 
    // yet, so this just verifies the engine doesn't choke)
    OrderPool pool(100);
    OrderBook book(&pool);

    book.processOrder(pool.acquire(1, 1500000, 100, Side::SELL));
    book.processOrder(pool.acquire(2, 1500000, 100, Side::BUY));

    assert(book.getTradeLog().size() == 1);
    assert(book.getTradeLog()[0].quantity == 100);
}

void test_aggressive_buy_crosses_multiple_levels() {
    // Buy at $155 should sweep all asks from $150 up to $155
    OrderPool pool(100);
    OrderBook book(&pool);

    book.processOrder(pool.acquire(1, 1500000, 10, Side::SELL));  // $150
    book.processOrder(pool.acquire(2, 1510000, 10, Side::SELL));  // $151
    book.processOrder(pool.acquire(3, 1520000, 10, Side::SELL));  // $152
    book.processOrder(pool.acquire(4, 1560000, 10, Side::SELL));  // $156 — above buy price

    // Limit buy @ $155 for 25 — should get 10@150, 10@151, 5@152, stop before $156
    book.processOrder(pool.acquire(5, 1550000, 25, Side::BUY));

    assert(book.getTradeLog().size() == 3);
    assert(book.getTradeLog()[2].price == 1520000);
    // Remaining 5 from $152 still on the book, plus $156
    assert(book.getBestAsk() == 1520000);
}

void test_vwap_calculation() {
    OrderPool pool(100);
    OrderBook book(&pool);

    // Trade 1: 100 @ $150.00
    book.processOrder(pool.acquire(1, 1500000, 100, Side::SELL));
    book.processOrder(pool.acquire(2, 1500000, 100, Side::BUY));

    // Trade 2: 200 @ $151.00
    book.processOrder(pool.acquire(3, 1510000, 200, Side::SELL));
    book.processOrder(pool.acquire(4, 1510000, 200, Side::BUY));

    // VWAP = (100*150 + 200*151) / (100+200) = (15000 + 30200) / 300 = 150.6667
    double vwap = book.getVWAP();
    assert(vwap > 150.66 && vwap < 150.67);
}

void test_empty_book_accessors() {
    OrderPool pool(100);
    OrderBook book(&pool);

    assert(book.getBestBid() == 0);
    assert(book.getBestAsk() == 0);
    assert(book.getVWAP() == 0.0);
    assert(book.getTradeLog().empty());
}

// ================================================================
// MAIN
// ================================================================

int main() {
    std::cout << "\n======= MATCHING ENGINE + MODIFY TEST SUITE =======\n\n";

    std::cout << "--- Basic Order Matching ---\n";
    RUN_TEST(test_limit_buy_matches_resting_sell);
    RUN_TEST(test_limit_sell_matches_resting_buy);
    RUN_TEST(test_no_match_when_prices_dont_cross);
    RUN_TEST(test_exact_fill_removes_both_orders);

    std::cout << "\n--- Market Orders ---\n";
    RUN_TEST(test_market_buy_sweeps_multiple_levels);
    RUN_TEST(test_market_sell_sweeps_bids);
    RUN_TEST(test_market_order_with_no_liquidity);

    std::cout << "\n--- Price-Time Priority ---\n";
    RUN_TEST(test_time_priority_fifo_at_same_price);
    RUN_TEST(test_price_priority_better_price_fills_first);

    std::cout << "\n--- Cancel Orders ---\n";
    RUN_TEST(test_cancel_removes_order_from_book);
    RUN_TEST(test_cancel_nonexistent_order_is_noop);
    RUN_TEST(test_cancel_middle_order_preserves_queue);

    std::cout << "\n--- Modify Orders (Cancel/Replace) ---\n";
    RUN_TEST(test_modify_qty_down_keeps_priority);
    RUN_TEST(test_modify_qty_up_loses_priority);
    RUN_TEST(test_modify_price_loses_priority);
    RUN_TEST(test_modify_to_better_price_moves_level);
    RUN_TEST(test_modify_nonexistent_order_is_noop);
    RUN_TEST(test_modify_updates_order_id_in_map);
    RUN_TEST(test_modify_bid_side);

    std::cout << "\n--- Full FIX Pipeline (Modify) ---\n";
    RUN_TEST(test_fix_cancel_replace_message);
    RUN_TEST(test_fix_cancel_replace_rejected_by_risk);

    std::cout << "\n--- Edge Cases ---\n";
    RUN_TEST(test_self_trade_at_same_price);
    RUN_TEST(test_aggressive_buy_crosses_multiple_levels);
    RUN_TEST(test_vwap_calculation);
    RUN_TEST(test_empty_book_accessors);

    std::cout << "\n====================================================\n";
    std::cout << "RESULTS: " << testsPassed << "/" << testsRun << " tests passed.\n";
    if (testsPassed == testsRun) {
        std::cout << "ALL TESTS PASSED.\n";
    } else {
        std::cout << "FAILURES DETECTED.\n";
        return 1;
    }
    std::cout << "====================================================\n\n";
    return 0;
}
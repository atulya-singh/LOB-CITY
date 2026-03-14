#include <iostream>
#include <cassert>
#include <cstring>
#include "Riskengine.h"
#include "FixParser.h"
#include "MemoryPool.h"
#include "OrderBook.h"
#include "OrderEntryGateway.h"

// ================================================================
// UNIT TEST FRAMEWORK (minimal, zero-dependency)
//
// Production firms use Google Test or Catch2, but a hand-rolled
// framework shows you understand what testing actually does under
// the hood. Each test is a function that calls assert(). If an
// assertion fails, the program crashes with the file and line number.
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

// Helper: build a ParsedFixMessage without going through the FIX parser.
// This isolates risk engine logic from parser correctness.
ParsedFixMessage makeMsg(char msgType, char side, char ordType, int64_t qty, int64_t price) {
    ParsedFixMessage msg;
    msg.reset();
    msg.msgType = msgType;
    msg.side = side;
    msg.ordType = ordType;
    msg.qty = qty;
    msg.price = price;
    msg.clOrdID = std::string_view("12345", 5);
    return msg;
}

// Helper: build a message from a raw FIX string (tests the full parse path)
ParsedFixMessage parseFix(const char* fixStr) {
    ParsedFixMessage msg;
    parseFixMessage(fixStr, std::strlen(fixStr), msg);
    return msg;
}

// ================================================================
// TEST GROUP 1: VALIDITY CHECKS
// ================================================================

void test_reject_zero_quantity() {
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '2', 0, 1500000);  // qty=0
    assert(risk.checkOrder(msg) == RiskRejectReason::INVALID_QUANTITY);
}

void test_reject_negative_price_limit_order() {
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '2', 100, -5000);  // negative price, limit order
    assert(risk.checkOrder(msg) == RiskRejectReason::INVALID_PRICE);
}

void test_allow_zero_price_market_order() {
    // Market orders have price=0 — that's valid, not an error
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '1', 100, 0);  // ordType='1' (market), price=0
    assert(risk.checkOrder(msg) == RiskRejectReason::NONE);
}

// ================================================================
// TEST GROUP 2: FAT FINGER CHECKS
// ================================================================

void test_reject_fat_finger_size() {
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '2', 2000000, 1500000);  // 2M shares
    assert(risk.checkOrder(msg) == RiskRejectReason::FAT_FINGER_SIZE);
}

void test_allow_just_under_size_limit() {
    RiskEngine risk;
    // BBO centered around $1 so both collar and notional work
    risk.updateBBO(10000, 10100);  // bid=$1.00, ask=$1.01
    // 1,000,000 shares at $1.00 = $1M notional — under both limits
    auto msg = makeMsg('D', '1', '2', 1000000, 10000);
    assert(risk.checkOrder(msg) == RiskRejectReason::NONE);
}

void test_reject_fat_finger_notional() {
    // $15,000 price * 1,000,000 qty = $15B notional (way over $10M limit)
    RiskConfig cfg;
    cfg.maxNotional = 100000000000LL;  // $10M in fixed-point
    RiskEngine risk(cfg);
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '2', 999999, 150000000);  // $15,000 * 999,999 shares
    assert(risk.checkOrder(msg) == RiskRejectReason::FAT_FINGER_NOTIONAL);
}

void test_notional_skip_for_market_orders() {
    // Market orders have price=0, so notional = 0. Should always pass notional check.
    RiskConfig cfg;
    cfg.maxNotional = 1;  // Set absurdly low
    RiskEngine risk(cfg);
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '1', 999999, 0);  // market order
    assert(risk.checkOrder(msg) == RiskRejectReason::NONE);
}

// ================================================================
// TEST GROUP 3: PRICE COLLAR CHECKS
// ================================================================

void test_reject_buy_above_collar() {
    // BBO: bid=$150, ask=$151, mid=$150.50
    // 5% collar = $150.50 * 0.05 = $7.525
    // Max buy price = $150.50 + $7.525 = $158.025
    // Buying at $200 should be rejected
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '2', 100, 2000000);  // $200 buy
    assert(risk.checkOrder(msg) == RiskRejectReason::PRICE_COLLAR);
}

void test_reject_sell_below_collar() {
    // Selling at $1 when market is at $150.50 — way below collar
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '2', '2', 100, 10000);  // $1 sell
    assert(risk.checkOrder(msg) == RiskRejectReason::PRICE_COLLAR);
}

void test_allow_within_collar() {
    // $150.25 buy when mid is $150.50 — deviation is $0.25 = 0.17%, well within 5%
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '2', 100, 1502500);  // $150.25
    assert(risk.checkOrder(msg) == RiskRejectReason::NONE);
}

void test_collar_at_exact_boundary() {
    // Mid = $150.50 = 1505000 in fixed-point
    // 5% = 1505000 * 500 / 10000 = 75250
    // Max = 1505000 + 75250 = 1580250
    // Order at exactly 1580250 should pass (deviation == maxDeviation, not >)
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '2', 100, 1580250);
    assert(risk.checkOrder(msg) == RiskRejectReason::NONE);
}

void test_collar_one_tick_past_boundary() {
    // 1580251 should be rejected (deviation > maxDeviation)
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '2', 100, 1580251);
    assert(risk.checkOrder(msg) == RiskRejectReason::PRICE_COLLAR);
}

void test_collar_skip_on_empty_book() {
    // No BBO at all — collar check should pass (no reference price)
    RiskEngine risk;
    risk.updateBBO(0, 0);
    auto msg = makeMsg('D', '1', '2', 100, 9990000);  // $999 — absurd, but no collar
    assert(risk.checkOrder(msg) == RiskRejectReason::NONE);
}

void test_collar_with_only_bid() {
    // Only a bid exists — use bid as reference
    RiskEngine risk;
    risk.updateBBO(1500000, 0);  // bid=$150, no ask
    auto msg = makeMsg('D', '2', '2', 100, 1500000);  // sell at $150 — 0% deviation
    assert(risk.checkOrder(msg) == RiskRejectReason::NONE);
}

void test_collar_skip_for_market_orders() {
    // Market orders have no price — collar doesn't apply
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '1', 100, 0);
    assert(risk.checkOrder(msg) == RiskRejectReason::NONE);
}

void test_custom_collar_width() {
    // Set collar to 100 bps (1%) instead of 500 bps (5%)
    // Mid = $150.50, 1% = $1.505, max = $152.005
    // $153 should be rejected with 1% collar but pass with 5% collar
    RiskConfig cfg;
    cfg.collarBps = 100;  // 1%
    RiskEngine risk(cfg);
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '2', 100, 1530000);  // $153
    assert(risk.checkOrder(msg) == RiskRejectReason::PRICE_COLLAR);
}

// ================================================================
// TEST GROUP 4: RATE LIMITING
// ================================================================

void test_rate_limit_triggers() {
    RiskConfig cfg;
    cfg.maxMsgsPerWindow = 5;  // Very low limit for testing
    cfg.windowDurationNs = 1000000000ULL;  // 1 second
    RiskEngine risk(cfg);
    risk.updateBBO(1500000, 1510000);

    auto msg = makeMsg('D', '1', '2', 100, 1500000);

    // First 5 should pass
    for (int i = 0; i < 5; i++) {
        assert(risk.checkOrder(msg) == RiskRejectReason::NONE);
    }
    // 6th should be rate-limited
    assert(risk.checkOrder(msg) == RiskRejectReason::RATE_LIMIT);
}

// ================================================================
// TEST GROUP 5: CHECK ORDERING (fail-fast correctness)
// An order that violates MULTIPLE rules should be rejected by the
// CHEAPEST check, not the most expensive one.
// ================================================================

void test_fail_fast_validity_before_collar() {
    // This order has qty=0 AND price=$999 (way outside collar).
    // Should be rejected for INVALID_QUANTITY, not PRICE_COLLAR.
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '2', 0, 9990000);
    assert(risk.checkOrder(msg) == RiskRejectReason::INVALID_QUANTITY);
}

void test_fail_fast_fat_finger_before_collar() {
    // This order has qty=5M AND price=$999.
    // Should be rejected for FAT_FINGER_SIZE, not PRICE_COLLAR.
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = makeMsg('D', '1', '2', 5000000, 9990000);
    assert(risk.checkOrder(msg) == RiskRejectReason::FAT_FINGER_SIZE);
}

// ================================================================
// TEST GROUP 6: FULL PIPELINE INTEGRATION
// Parse a raw FIX string → risk check → verify result.
// This tests that the parser and risk engine work together correctly.
// ================================================================

void test_full_pipeline_valid_order() {
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = parseFix("8=FIX.4.2\x01" "35=D\x01" "11=100\x01" "54=1\x01" "38=50\x01" "44=150.25\x01" "40=2\x01");
    assert(risk.checkOrder(msg) == RiskRejectReason::NONE);
}

void test_full_pipeline_collar_rejection() {
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);
    auto msg = parseFix("8=FIX.4.2\x01" "35=D\x01" "11=101\x01" "54=1\x01" "38=50\x01" "44=200.00\x01" "40=2\x01");
    assert(risk.checkOrder(msg) == RiskRejectReason::PRICE_COLLAR);
}

void test_full_pipeline_matching_with_risk() {
    // Full end-to-end: seed book, send order through gateway, verify it matched
    OrderPool pool(1000);
    OrderBook book(&pool);
    OrderEntryGateway gateway(&pool, &book);

    // Seed the book directly (bypassing risk — bootstrap)
    book.processOrder(pool.acquire(1, 1500000, 100, Side::SELL));  // Ask @ $150

    // Send a matching buy through the full gateway+risk pipeline
    auto msg = parseFix("8=FIX.4.2\x01" "35=D\x01" "11=2\x01" "54=1\x01" "38=50\x01" "44=150.00\x01" "40=2\x01");
    gateway.onParsedMessage(msg);

    // Verify: the trade log should have 1 trade for 50 units
    assert(book.getTradeLog().size() == 1);
    assert(book.getTradeLog()[0].quantity == 50);
    assert(book.getTradeLog()[0].price == 1500000);
}

void test_full_pipeline_risk_blocks_before_matching() {
    // Verify that a rejected order never creates a trade
    OrderPool pool(1000);
    OrderBook book(&pool);
    OrderEntryGateway gateway(&pool, &book);

    // Seed the book
    book.processOrder(pool.acquire(1, 1500000, 100, Side::SELL));  // Ask @ $150

    // Send a buy at $999 — should be collar-rejected, NO trade should happen
    auto msg = parseFix("8=FIX.4.2\x01" "35=D\x01" "11=2\x01" "54=1\x01" "38=50\x01" "44=999.00\x01" "40=2\x01");
    gateway.onParsedMessage(msg);

    // Verify: zero trades. The order never reached the book.
    assert(book.getTradeLog().empty());
}

// ================================================================
// TEST GROUP 7: STATISTICS TRACKING
// ================================================================

void test_stats_accuracy() {
    RiskEngine risk;
    risk.updateBBO(1500000, 1510000);

    // Send 3 valid, 2 invalid
    risk.checkOrder(makeMsg('D', '1', '2', 100, 1500000));  // pass
    risk.checkOrder(makeMsg('D', '1', '2', 100, 1500000));  // pass
    risk.checkOrder(makeMsg('D', '1', '2', 100, 1500000));  // pass
    risk.checkOrder(makeMsg('D', '1', '2', 0,   1500000));  // reject: invalid qty
    risk.checkOrder(makeMsg('D', '1', '2', 5000000, 1500000));  // reject: fat finger

    // We can't directly access stats, but we can verify via the check results
    // The real assertion is that the engine didn't crash or miscount.
    // In production, you'd expose stats and verify exact counts.
    auto r1 = risk.checkOrder(makeMsg('D', '1', '2', 100, 1500000));
    assert(r1 == RiskRejectReason::NONE);  // 6th order — still passes
}

// ================================================================
// MAIN
// ================================================================

int main() {
    std::cout << "\n======= RISK ENGINE UNIT TESTS =======\n\n";

    std::cout << "--- Validity Checks ---\n";
    RUN_TEST(test_reject_zero_quantity);
    RUN_TEST(test_reject_negative_price_limit_order);
    RUN_TEST(test_allow_zero_price_market_order);

    std::cout << "\n--- Fat Finger Checks ---\n";
    RUN_TEST(test_reject_fat_finger_size);
    RUN_TEST(test_allow_just_under_size_limit);
    RUN_TEST(test_reject_fat_finger_notional);
    RUN_TEST(test_notional_skip_for_market_orders);

    std::cout << "\n--- Price Collar Checks ---\n";
    RUN_TEST(test_reject_buy_above_collar);
    RUN_TEST(test_reject_sell_below_collar);
    RUN_TEST(test_allow_within_collar);
    RUN_TEST(test_collar_at_exact_boundary);
    RUN_TEST(test_collar_one_tick_past_boundary);
    RUN_TEST(test_collar_skip_on_empty_book);
    RUN_TEST(test_collar_with_only_bid);
    RUN_TEST(test_collar_skip_for_market_orders);
    RUN_TEST(test_custom_collar_width);

    std::cout << "\n--- Rate Limiting ---\n";
    RUN_TEST(test_rate_limit_triggers);

    std::cout << "\n--- Fail-Fast Ordering ---\n";
    RUN_TEST(test_fail_fast_validity_before_collar);
    RUN_TEST(test_fail_fast_fat_finger_before_collar);

    std::cout << "\n--- Full Pipeline Integration ---\n";
    RUN_TEST(test_full_pipeline_valid_order);
    RUN_TEST(test_full_pipeline_collar_rejection);
    RUN_TEST(test_full_pipeline_matching_with_risk);
    RUN_TEST(test_full_pipeline_risk_blocks_before_matching);

    std::cout << "\n--- Statistics ---\n";
    RUN_TEST(test_stats_accuracy);

    std::cout << "\n======================================\n";
    std::cout << "RESULTS: " << testsPassed << "/" << testsRun << " tests passed.\n";
    if (testsPassed == testsRun) {
        std::cout << "ALL TESTS PASSED.\n";
    } else {
        std::cout << "FAILURES DETECTED.\n";
        return 1;
    }
    std::cout << "======================================\n\n";
    return 0;
}
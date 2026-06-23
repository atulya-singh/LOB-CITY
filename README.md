# Ultra-Low Latency Limit Order Book Engine

A multi-threaded exchange emulator in C++ with a matching engine, pre-trade risk controls, and a market-making strategy with inventory risk management. Built on modern HFT systems principles — zero heap allocations on the critical path, lock-free inter-thread communication, no system calls on the matching thread, and a **42 ns median / ~800 ns p99.9** matching latency over ~900K orders.

The engine is organized into domain modules (`core`, `gateway`, `risk`, `marketdata`, `strategy`), with runnable apps, unit-test suites, and Python tooling for load testing, market data, and PnL visualization.

## Architecture

Three busy-wait threads connected by lock-free SPSC ring buffers. The hot path — the matching thread — never makes a system call: order ingress and market-data egress are both offloaded to neighbouring threads through queues.

```
  TCP Client              ┌──────────────────────────────────────────────────┐
  (replayer.py) ──TCP───► │ THREAD 1 — Network I/O                            │
                          │   recv() → FIX Parser → orderQueue.push()         │
                          └─────────────────────┬────────────────────────────┘
                                                │  orderQueue  (lock-free SPSC)
                                                ▼
                          ┌──────────────────────────────────────────────────┐
                          │ THREAD 2 — Matching Engine  (the hot path)        │
                          │   pop → Risk Engine → Memory Pool → Order Book     │
                          │   (price-time priority)                            │
                          │   on top-of-book change → bboQueue.push()          │
                          │                          └─ no syscall, just a store│
                          └─────────────────────┬────────────────────────────┘
                                                │  bboQueue  (lock-free SPSC)
                                                ▼
  UDP Multicast           ┌──────────────────────────────────────────────────┐
  (market_data_  ◄──UDP── │ THREAD 3 — Market Data Publisher                  │
   listener.py)           │   pop → sendto()   ← the syscall lives here, alone │
                          └──────────────────────────────────────────────────┘
```

The network thread reads TCP bytes, parses FIX, and pushes to `orderQueue`. The matching thread busy-waits on it, runs pre-trade risk checks, and matches. When the top of book moves it pushes a BBO snapshot to `bboQueue` and immediately returns to matching — the `sendto()` UDP multicast is performed by a dedicated publisher thread, keeping the kernel boundary off the critical path entirely.

## Performance

Per-order matching latency measured over **~900K orders** of synthetic flow (mixed limit / market / cancel, both sides, prices spread across a ±20-tick band), built with `-O3 -march=native` on Apple Silicon (macOS), no kernel-bypass networking. Reproduce with `latency_bench` (see Build and Run).

| Statistic | Latency |
|:---|:---|
| Mean | ~56 ns |
| Median (p50) | **42 ns** |
| p90 | 84 ns |
| p99 | 167 ns |
| p99.9 | ~800 ns |
| p99.99 | ~1,250 ns |

Throughput sustains **~24M orders/sec** through the matching engine and **~14M msg/sec** end-to-end (FIX parse → risk → gateway → match). Max over a run reaches a few hundred microseconds across a handful of cold-start samples — first-touch page faults and one-off OS preemptions on a non–core-isolated laptop — past the p99.99 and not representative of steady state.

**Tail-latency case study.** The market-data publish was originally a synchronous `sendto()` *inside* the timed matching path. Profiling showed a bimodal distribution: a ~42 ns median (orders that didn't move the book) against a ~10 µs p99.9 (orders that did, and paid the kernel crossing). Moving the `sendto()` onto a dedicated publisher thread fed by a second lock-free ring erased that tail — the sub-microsecond p99.9 above is the result — with no change to the median, confirming the syscall was the sole tail driver. Throughput nearly doubled (13M → 24M orders/sec).

## Design Decisions

**No system calls on the hot path** — the matching thread never calls into the kernel. Market data is handed to a publisher thread via a lock-free ring, so a blocking/preemptible `sendto()` can never stall order matching. This is the single biggest contributor to the collapsed latency tail (see case study above).

**Lock-free SPSC ring buffers for all thread handoff** — `std::atomic` head/tail with acquire/release ordering, power-of-2 capacity (modulo compiles to a bitwise AND). No mutexes, no condition variables, no context switches between stages.

**Busy-wait over condition variables** — each thread spins at 100% on its own core. Wake-up latency is bounded by cache-coherence propagation (~50 ns) rather than OS scheduler wake-up (~10,000 ns+). The classic latency-sensitive tradeoff: dedicate a core to polling for deterministic response.

**Zero heap allocation on the critical path** — `Order` objects come from a pre-allocated pool (O(1) free-list acquire/release), aligned to 64-byte cache lines to prevent false sharing. No `new`/`delete` while matching.

**Fixed-point integer prices** — prices are `int64_t` with 4 implied decimals ($150.00 = 1,500,000). Eliminates floating-point arithmetic and comparison error from the matching engine; price math is exact.

**O(1) cancel and modify** — each price level is an intrusive doubly-linked list and every live order is indexed in a hash map, so any order is unlinked by ID in constant time. A `std::deque` would force an O(n) scan to remove from the middle.

**Risk checks before pool allocation** — rejected orders never consume a pool slot or touch the book, so a flood of invalid orders can't exhaust memory or slow matching for valid flow.

## Components

**Matching Engine** — `core/OrderBook.{h,cpp}`, `core/types.h`
`std::map` per side (bids descending, asks ascending); each price level is a doubly-linked list of pooled `Order` structs. **Price-time priority** (best price first, FIFO within a level). Supports limit, market (multi-level sweep), cancel, and cancel/replace. Cancel/replace keeps queue position on a same-price size *decrease*, loses it on a price change or size *increase* — standard exchange time-priority rules.

**Pre-Trade Risk Engine** — `risk/Riskengine.h`
Five fail-fast checks, cheapest first: validity → fat-finger size → fat-finger notional → 5% price collar → rate limit. Rejected orders never reach the pool or book. Tracks pass rate and rejection breakdown.

**Lock-Free Ring Buffers** — `core/RingBuffer.h`, `marketdata/RingBufferBBO.h`
SPSC queues with acquire/release atomics. One carries parsed orders (network → matcher), the other carries BBO snapshots (matcher → publisher). Power-of-2 capacity, no locks or syscalls.

**Memory Pool** — `core/MemoryPool.h`
200,000 `Order` objects pre-allocated at startup; O(1) `acquire()`/`release()` over a free list; 64-byte aligned. Zero allocation on the hot path.

**FIX Protocol Parser** — `gateway/FixParser.{h,cpp}`
Single-pass, zero-allocation FIX 4.2 parser. Tag=value pairs over SOH delimiters into a flat struct; fixed-point prices; order IDs held as `string_view` into the original buffer (no copies). Handles New Order (35=D), Cancel (35=F), Cancel/Replace (35=G).

**Market Data Feed** — `marketdata/UdpPublisher.h`, `marketdata/MarketData.h`
Packed `BboMessage` (cache-line aligned, no padding) broadcast over UDP multicast (239.255.0.1:3050) from the publisher thread. Raw struct on the wire — no serialization.

**Market-Making Strategy** — `strategy/Marketmaker.h`, `apps/Simulator.cpp`
Avellaneda-Stoikov–inspired quoter with three inventory controls: **inventory skew** (both quotes shift with net position, a self-correcting pull toward flat), **position limits** (±500 share cap, size tapers near the limit), and a **kill switch** (cancels all quotes if PnL breaches a floor). The simulator drives it with synthetic order flow — random-walk prices, exponential sizes, and a realistic limit/market/cancel/quiet tick mix.

**Sample Backtest (100,000 ticks):**

| Total Fills | Total PnL | Realized PnL | Max Drawdown | PnL / Fill | Max Position |
|:---|:---|:---|:---|:---|:---|
| 1,435 | $9,379 | $7,737 | $3,957 | $6.54 | ±500 |

![Market Maker Backtest](docs/pnl_curve_report.png)

## Test Suites

49 automated tests across two suites.

- **Risk Engine (24)** — validity, fat-finger rejection, exact price-collar boundaries, rate limiting, fail-fast ordering, and full FIX-parse → risk → match → trade-log integration.
- **Matching Engine (25)** — limit/market matching, price-time priority, cancel correctness (incl. middle-of-queue), modify with priority preservation, full FIX cancel/replace, and edge cases (empty book, multi-level sweeps, VWAP).

## Build and Run

Run from the repository root. Shared engine sources live in `src/`, apps in `apps/`, tests in `tests/`.

**Exchange server:**
```bash
g++ -O3 -std=c++17 -march=native -pthread \
    apps/main.cpp apps/tests.cpp \
    src/gateway/FixParser.cpp src/core/OrderBook.cpp -o lob_server
./lob_server
```

**Market data listener** (separate terminal):
```bash
python3 scripts/market_data_listener.py
```

**Load injector** (separate terminal — 100K FIX orders over TCP):
```bash
python3 scripts/replayer.py
```

**Latency benchmark** (drives ~1M orders, prints the percentile report above):
```bash
g++ -O3 -std=c++17 -march=native -pthread \
    apps/latency_bench.cpp src/gateway/FixParser.cpp src/core/OrderBook.cpp -o latency_bench
./latency_bench
```

**Market maker backtest:**
```bash
g++ -O3 -std=c++17 -march=native -pthread \
    apps/Simulator.cpp src/gateway/FixParser.cpp src/core/OrderBook.cpp -o simulator
./simulator
python3 scripts/plot_backtest.py    # generates docs/pnl_curve_report.png
```

**Test suites:**
```bash
g++ -O3 -std=c++17 -pthread \
    tests/test_risk_engine.cpp src/gateway/FixParser.cpp src/core/OrderBook.cpp -o test_risk
g++ -O3 -std=c++17 -pthread \
    tests/test_matching_engine.cpp src/gateway/FixParser.cpp src/core/OrderBook.cpp -o test_engine
./test_risk && ./test_engine
```

## File Structure

```
├── src/                              Shared engine library
│   ├── core/
│   │   ├── types.h                   Core types: Order, PriceLevel, Trade, Side
│   │   ├── OrderBook.h / .cpp        Matching engine (price-time priority)
│   │   ├── MemoryPool.h              Pre-allocated object pool for Order structs
│   │   ├── RingBuffer.h              Lock-free SPSC queue — order ingress
│   │   └── LatencyTracker.h          Nanosecond percentile statistics
│   ├── gateway/
│   │   ├── FixParser.h / .cpp        Zero-allocation FIX 4.2 protocol parser
│   │   └── OrderEntryGateway.h       Message routing + FIX-to-internal translation
│   ├── risk/
│   │   └── Riskengine.h              Pre-trade risk checks (5-stage pipeline)
│   ├── marketdata/
│   │   ├── MarketData.h              Packed BBO struct for UDP wire format
│   │   ├── RingBufferBBO.h           Lock-free SPSC queue — market-data egress
│   │   └── UdpPublisher.h            UDP multicast publisher
│   └── strategy/
│       └── Marketmaker.h             Market-making strategy with inventory skew
├── apps/                             Runnable entry points
│   ├── main.cpp                      Exchange server (3-thread pipeline)
│   ├── latency_bench.cpp             1M-order latency benchmark (percentile report)
│   ├── Simulator.cpp                 Backtest harness with synthetic order flow
│   └── tests.h / tests.cpp           In-process functional + benchmark routines
├── tests/                            Standalone unit-test suites
│   ├── test_risk_engine.cpp          Risk engine + FIX pipeline tests
│   └── test_matching_engine.cpp      Matching engine + cancel/replace tests
├── scripts/                          Python tooling
│   ├── replayer.py                   TCP load injector (100K FIX messages)
│   ├── market_data_listener.py       UDP multicast BBO receiver
│   └── plot_backtest.py              PnL curve visualization
└── docs/
    └── pnl_curve_report.png          Sample backtest equity curve
```

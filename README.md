# Ultra-Low Latency Limit Order Book (LOB) Emulator

An open-source, multi-threaded High-Frequency Trading (HFT) exchange emulator built in C++. Designed for quantitative developers and algorithmic traders to backtest trading strategies locally with deterministic, sub-microsecond matching latency and zero garbage-collection overhead.



## ⚡ Core Architecture & Features

This engine is engineered following modern HFT systems design principles, entirely bypassing the C++ standard library allocations in the critical path.

* **Zero-Allocation Engine:** Utilizes a custom pre-allocated Memory Pool for order objects. Eliminates `new`/`delete` calls and prevents heap fragmentation during live trading.
* **Lock-Free Concurrency:** Decouples network I/O from the matching engine using a custom **Single-Producer, Single-Consumer (SPSC) Ring Buffer**. Uses C++ `std::atomic` memory barriers (`acquire`/`release`) instead of slow `std::mutex` locks.
* **Custom FIX Parser:** Maps incoming raw network bytes directly to tightly packed C++ structs without using `std::string` or dynamic memory.
* **Asynchronous TCP Order Entry:** Handles incoming FIX Protocol orders (New Order Single, Cancel/Replace) over standard TCP sockets.
* **UDP Multicast Market Data Feed:** Disseminates Top-of-Book (BBO) market data updates via a custom, tightly packed binary protocol (similar to Nasdaq ITCH) over UDP multicast, ensuring $O(1)$ network duplication.

## 🚀 Performance Benchmarks

Benchmarked on a standard Apple Silicon architecture (macOS) without kernel-bypass networking (e.g., DPDK/Solarflare). 

| Metric | Performance | Notes |
| :--- | :--- | :--- |
| **Matching Engine Latency** | `< 100 ns` | Pure software matching execution time (L1/L2 cache). |
| **Pipeline Latency** | `< 150 ns` | Full path: String Parsing -> Memory Allocation -> Execution. |
| **Network Ingestion (TCP)** | `> 1.5 Million ops/sec` | Peak throughput utilizing the SPSC lock-free queue buffer. |



## 🛠️ Tech Stack
* **Core:** C++ (compiled with `-O3 -march=native`)
* **Threading:** `<thread>`, `<atomic>` (Lock-free synchronization)
* **Networking:** POSIX Sockets (TCP stream ingress, UDP datagram egress)
* **Load Testing / Tooling:** Python 3 (`socket`, `struct`)

## 🖥️ How to Build and Run

### 1. Compile the C++ Engine
Compile the server with maximum optimizations and hardware-specific instruction sets enabled:
```bash
g++ -O3 -march=native -pthread main.cpp FixParser.cpp OrderBook.cpp -o lob_server

### 2. Start the Exchange

Run the compiled executable. The engine will automatically pin the Network IO and Matching Engine to separate threads and begin listening on TCP Port 5050.
```./lob_server

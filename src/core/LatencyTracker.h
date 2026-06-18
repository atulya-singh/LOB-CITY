#pragma once 
#include <cstdint>
#include <vector>
#include <algorithm>
#include <iostream>
#include <chrono>

class LatencyTracker{
    private:
    std::vector<uint64_t> samples;

    public: 
    LatencyTracker(){
        samples.reserve(1000000);
    }

    inline void record(uint64_t startNs, uint64_t endNs){
        samples.push_back(endNs - startNs);
    }
    void printReport() const{
        if (samples.empty()){
            std::cout << "No samples recorded.\n";
            return;
        }

        std::vector<uint64_t> sorted = samples;
        std::sort(sorted.begin(), sorted.end());

        size_t count = sorted.size();

        // Percentile formula: index = (percentile / 100) * count
        // We use the index directly — this is the nearest-rank method
        auto percentile = [&](double p) -> uint64_t {
            size_t idx = static_cast<size_t>(p / 100.0 * count);
            if (idx >= count) idx = count - 1;
            return sorted[idx];
        };

        uint64_t total = 0;
        for (uint64_t s : sorted) total += s;
        double avg = static_cast<double>(total) / count;

        std::cout << "\n========== LATENCY REPORT ==========\n";
        std::cout << "Samples:    " << count << " orders\n";
        std::cout << "Average:    " << avg << " ns\n";
        std::cout << "Min:        " << sorted.front() << " ns\n";
        std::cout << "p50:        " << percentile(50) << " ns\n";
        std::cout << "p95:        " << percentile(95) << " ns\n";
        std::cout << "p99:        " << percentile(99) << " ns\n";
        std::cout << "p99.9:      " << percentile(99.9) << " ns\n";
        std::cout << "Max:        " << sorted.back() << " ns\n";
        std::cout << "=====================================\n\n";
    }
};
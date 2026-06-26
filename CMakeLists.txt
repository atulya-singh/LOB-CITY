cmake_minimum_required(VERSION 3.10)


project(LatencyBench CXX)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


add_executable(latency_bench
    apps/latency_bench.cpp
    src/gateway/FixParser.cpp
    src/core/OrderBook.cpp
)


target_compile_options(latency_bench PRIVATE
    -O3
    -march=native
)


set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(latency_bench PRIVATE Threads::Threads)
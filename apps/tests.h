#pragma once

class OrderBook;
class OrderPool;
class OrderEntryGateway;


void runFunctionalTest(OrderBook& book, OrderPool& pool);

void runEndToEndBenchmark(OrderEntryGateway& gateway);

void runRiskEngineTest(OrderBook& book, OrderPool& pool, OrderEntryGateway& gateway);

void runPipelineTest(OrderEntryGateway& gateway);

void runBenchmark(OrderBook& book, OrderPool& pool);
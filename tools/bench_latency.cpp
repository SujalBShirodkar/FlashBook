#include "engine.h"
#include "spsc_queue.h"
#include "latency.h"
#include "order.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

int main(int argc, char* argv[]) {
    int num_orders = 100000;
    if (argc >= 2) num_orders = atoi(argv[1]);

    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║         FlashBook — Latency Benchmark            ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");
    printf("Orders: %d\n\n", num_orders);

    auto& clk = globalClock();
    printf("TSC freq: %.3f GHz\n\n", clk.tscFreqGHz());

    OrderQueue queue;

    Engine engine(queue);
    engine.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    printf("Pushing %d orders...\n", num_orders);

    uint64_t base_price = 1800000; 

    for (int i = 0; i < num_orders; ++i) {
        Order o{};
        o.order_id = static_cast<uint64_t>(i + 1);
        o.quantity = 1;
        o.type     = OrderType::LIMIT;

        if (i % 2 == 0) {
            o.side  = Side::BUY;
            o.price = base_price;
        } else {
            o.side  = Side::SELL;
            o.price = base_price;
        }

        o.recv_tsc = rdtsc();

        while (!queue.push(o)) {
            __builtin_ia32_pause();
        }
    }

    printf("All orders pushed. Waiting for engine to drain...\n");

    while (engine.ordersProcessed() < static_cast<uint64_t>(num_orders)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    engine.stop();
    engine.join();

    printf("\nOrders processed : %llu\n",
           (unsigned long long)engine.ordersProcessed());
    printf("Trades executed  : %llu\n\n",
           (unsigned long long)engine.tradesExecuted());

    engine.printLatency();

    printf("─────────────────────────────────────────────\n");
    printf("TSC read overhead (rdtsc round-trip):\n");
    constexpr int TSC_ITERS = 1'000'000;
    uint64_t tsc_start = rdtsc();
    volatile uint64_t sink = 0;
    for (int i = 0; i < TSC_ITERS; ++i) {
        sink = rdtsc();
    }
    uint64_t tsc_end = rdtscp();
    double tsc_overhead = clk.ticksToNs(tsc_end - tsc_start) / TSC_ITERS;
    printf("  %.2f ns per rdtsc() call\n", tsc_overhead);
    printf("  (subtract from latency measurements for pure engine cost)\n\n");
    (void)sink;

    return 0;
}
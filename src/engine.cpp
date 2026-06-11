#include "engine.h"
#include "latency.h"
#include "logger.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <cstring>

Engine::Engine(OrderQueue& queue)
    : queue_(queue)
    , book_([this](const Trade& t) {
        trades_executed_.fetch_add(1, std::memory_order_relaxed);

        printf("\n  ╔══ TRADE EXECUTED ══════════════════╗\n");
        printf("  ║  buy_id  %-6llu  sell_id %-6llu  ║\n",
               (unsigned long long)t.buy_order_id,
               (unsigned long long)t.sell_order_id);
        printf("  ║  price   %-12s  qty %-6u  ║\n",
               priceToString(t.price).c_str(),
               t.quantity);
        printf("  ╚════════════════════════════════════╝\n\n");
        fflush(stdout);
    })
{}

Engine::~Engine() {
    if (running_.load()) {
        stop();
        join();
    }
}

void Engine::setCpuAffinity(int core_id) {
    cpu_affinity_ = core_id;
}

void Engine::start() {
    running_.store(true);

    int ret = pthread_create(&thread_, nullptr, threadEntry, this);
    if (ret != 0) {
        throw std::runtime_error(
            std::string("pthread_create failed: ") + strerror(ret));
    }

    LOG("ENGINE", "Thread spawned");
}

void Engine::stop() {
    running_.store(false, std::memory_order_relaxed);
}

void Engine::join() {
    pthread_join(thread_, nullptr);
}

void Engine::printBook() const {
    book_.prettyPrint();
}

void Engine::run() {
    if (cpu_affinity_ >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_affinity_, &cpuset);
        int ret = pthread_setaffinity_np(
            pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (ret != 0) {
            LOG("WARN", "Failed to set CPU affinity to core %d: %s",
                cpu_affinity_, strerror(ret));
        } else {
            LOG("ENGINE", "Pinned to CPU core %d", cpu_affinity_);
        }
    }

    LOG("ENGINE", "Matching engine running. Waiting for orders...");

    while (running_.load(std::memory_order_relaxed)) {
        auto maybe_order = queue_.pop();

        if (!maybe_order) {
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
            continue;
        }

        const Order& order = *maybe_order;
        orders_processed_.fetch_add(1, std::memory_order_relaxed);

        if (order.recv_tsc != 0) {
            uint64_t now_tsc   = rdtscp();
            uint64_t ticks     = now_tsc - order.recv_tsc;
            double   latency_ns = globalClock().ticksToNs(ticks);
            latency_.record(latency_ns);
        }

        if (order.type == OrderType::CANCEL) {
            book_.cancelOrder(order.order_id);
        } else {
            book_.addAndMatch(order);
        }
    }

    while (true) {
        auto maybe_order = queue_.pop();
        if (!maybe_order) break;
        const Order& order = *maybe_order;
        orders_processed_.fetch_add(1, std::memory_order_relaxed);
        if (order.type == OrderType::CANCEL)
            book_.cancelOrder(order.order_id);
        else
            book_.addAndMatch(order);
    }

    LOG("ENGINE", "Engine stopped. orders=%llu trades=%llu",
        (unsigned long long)orders_processed_.load(),
        (unsigned long long)trades_executed_.load());
}
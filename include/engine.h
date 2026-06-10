#pragma once
#include "order_book.h"
#include "spsc_queue.h"
#include "memory_pool.h"
#include "order.h"
#include "logger.h"
#include <atomic>
#include <cstdint>
#include <pthread.h>

using OrderQueue = SPSCQueue<Order, 4096>;

class Engine {
public:
    explicit Engine(OrderQueue& queue);
    ~Engine();

    void setCpuAffinity(int core_id);

    void start();

    void stop();

    void join();

    uint64_t ordersProcessed() const { return orders_processed_.load(); }
    uint64_t tradesExecuted()  const { return trades_executed_.load(); }

    void printBook() const;

private:
    void run();   

    static void* threadEntry(void* arg) {
        static_cast<Engine*>(arg)->run();
        return nullptr;
    }

    OrderQueue&   queue_;
    OrderBook     book_;
    MemoryPool<Order, 8192> pool_;  

    pthread_t     thread_{};
    int           cpu_affinity_{-1};  
    std::atomic<bool> running_{false};

    std::atomic<uint64_t> orders_processed_{0};
    std::atomic<uint64_t> trades_executed_{0};
};
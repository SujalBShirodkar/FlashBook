#include "memory_pool.h"
#include "order.h"
#include "logger.h"
#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include <chrono>
#include <stdexcept>

using Clock = std::chrono::steady_clock;
using NS    = std::chrono::nanoseconds;

static uint64_t nowNs() {
    return std::chrono::duration_cast<NS>(
        Clock::now().time_since_epoch()).count();
}

template<typename T>
inline void doNotOptimize(T const& val) {
    asm volatile("" : : "r,m"(val) : "memory");
}

static Order makeOrder(uint64_t id, uint64_t price, uint32_t qty, Side side) {
    Order o{};
    o.order_id = id;
    o.price    = price;
    o.recv_tsc = 0;
    o.quantity = qty;
    o.side     = side;
    o.type     = OrderType::LIMIT;
    return o;
}

void test_basic() {
    printf("\n[TEST 1] Basic alloc/free\n");

    MemoryPool<Order, 8> pool;
    assert(pool.freeCount() == 8);
    assert(pool.usedCount() == 0);

    Order* a = pool.alloc();
    Order* b = pool.alloc();
    Order* c = pool.alloc();

    new (a) Order(makeOrder(1, 1802500, 100, Side::BUY));
    new (b) Order(makeOrder(2, 1799500,  50, Side::SELL));
    new (c) Order(makeOrder(3, 1801000, 200, Side::BUY));

    assert(pool.usedCount() == 3);
    assert(pool.freeCount() == 5);
    assert(a->order_id == 1);
    assert(b->order_id == 2);
    assert(c->order_id == 3);
    assert(pool.owns(a));
    assert(pool.owns(b));

    printf("  Allocated 3 orders. used=%zu free=%zu\n",
           pool.usedCount(), pool.freeCount());

    pool.free(a);
    pool.free(b);
    pool.free(c);

    assert(pool.freeCount() == 8);
    assert(pool.usedCount() == 0);
    printf("  Freed 3 orders. used=%zu free=%zu\n",
           pool.usedCount(), pool.freeCount());
    printf("  PASS\n");
}

void test_exhaustion() {
    printf("\n[TEST 2] Pool exhaustion\n");

    MemoryPool<Order, 4> pool;
    std::vector<Order*> ptrs;

    for (int i = 0; i < 4; ++i) {
        Order* o = pool.alloc();
        new (o) Order(makeOrder((uint64_t)i, 1800000, 10, Side::BUY));
        ptrs.push_back(o);
    }
    assert(pool.freeCount() == 0);
    printf("  Pool full. freeCount=%zu\n", pool.freeCount());

    bool threw = false;
    try {
        pool.alloc();
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    assert(threw && "Expected std::bad_alloc when pool is full");
    printf("  5th alloc threw std::bad_alloc as expected.\n");

    pool.free(ptrs[0]);
    Order* recycled = pool.alloc();
    assert(recycled != nullptr);
    printf("  After freeing one slot, alloc succeeded. PASS\n");

    pool.free(recycled);
    for (size_t i = 1; i < ptrs.size(); ++i) pool.free(ptrs[i]);
}

void test_lifo_reuse() {
    printf("\n[TEST 3] LIFO freelist reuse\n");

    MemoryPool<Order, 4> pool;

    Order* a = pool.alloc();
    Order* b = pool.alloc();

    pool.free(b);
    pool.free(a);

    Order* first  = pool.alloc();
    Order* second = pool.alloc();

    assert(first  == a && "Expected LIFO: a should be returned first");
    assert(second == b && "Expected LIFO: b should be returned second");
    printf("  LIFO order confirmed. PASS\n");

    pool.free(first);
    pool.free(second);
}

void test_benchmark() {
    printf("\n[TEST 4] Speed benchmark (1,000,000 iterations)\n");
    printf("  (anti-optimization barrier applied — results are real)\n");

    constexpr int ITERS = 1'000'000;

    {
        MemoryPool<Order, 1024> pool;
        uint64_t t0 = nowNs();

        for (int i = 0; i < ITERS; ++i) {
            Order* o = pool.alloc();
            new (o) Order(makeOrder((uint64_t)i, 1800000, 100, Side::BUY));
            doNotOptimize(o->order_id);
            pool.free(o);
        }

        uint64_t t1 = nowNs();
        double ns_per_op = (double)(t1 - t0) / ITERS;
        printf("  MemoryPool : %9llu ns total  %6.2f ns/op\n",
               (unsigned long long)(t1 - t0), ns_per_op);
    }

    {
        uint64_t t0 = nowNs();

        for (int i = 0; i < ITERS; ++i) {
            Order* o = static_cast<Order*>(malloc(sizeof(Order)));
            new (o) Order(makeOrder((uint64_t)i, 1800000, 100, Side::BUY));
            doNotOptimize(o->order_id);
            o->~Order();
            ::free(o);
        }

        uint64_t t1 = nowNs();
        double ns_per_op = (double)(t1 - t0) / ITERS;
        printf("  malloc/free: %9llu ns total  %6.2f ns/op\n",
               (unsigned long long)(t1 - t0), ns_per_op);
    }

    printf("  (Pool should be 5-20x faster than malloc)\n");
}

int main() {
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║        FlashBook — Memory Pool Tests         ║\n");
    printf("╚══════════════════════════════════════════════╝\n");

    test_basic();
    test_exhaustion();
    test_lifo_reuse();
    test_benchmark();

    printf("\n[ALL TESTS PASSED]\n");
    return 0;
}
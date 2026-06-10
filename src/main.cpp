#include "udp_receiver.h"
#include "spsc_queue.h"
#include "order.h"
#include "logger.h"
#include <pthread.h>
#include <csignal>
#include <cstdio>
#include <atomic>
#include <thread>

static SPSCQueue<Order, 4096> g_queue;

static UDPReceiver* g_receiver = nullptr;
static std::atomic<bool> g_shutdown{false};

void handleSignal(int sig) {
    fprintf(stderr, "\n[SIGNAL] Caught signal %d — shutting down...\n", sig);
    g_shutdown.store(true);
    if (g_receiver) g_receiver->stop();
}

void printOrder(const char* tag, const Order& o) {
    printf("[%-6s] id=%-6llu  %-4s  %-7s  qty=%-6u  price=%s\n",
           tag,
           (unsigned long long)o.order_id,
           sideToString(o.side),
           typeToString(o.type),
           o.quantity,
           priceToString(o.price).c_str());
    fflush(stdout);
}

void consumerThread() {
    LOG("ENGINE", "Consumer thread started. Spinning on queue...");

    uint64_t processed = 0;

    while (!g_shutdown.load(std::memory_order_relaxed)) {
        auto order = g_queue.pop();
        if (!order) {
            continue;
        }

        printOrder("ENGINE", *order);
        ++processed;
    }

    while (true) {
        auto order = g_queue.pop();
        if (!order) break;
        printOrder("ENGINE", *order);
        ++processed;
    }

    LOG("ENGINE", "Consumer thread stopped. processed=%llu",
        (unsigned long long)processed);
}

int main(int argc, char* argv[]) {
    uint16_t port = 9001;
    if (argc >= 2) {
        int p = atoi(argv[1]);
        if (p > 0 && p < 65536)
            port = static_cast<uint16_t>(p);
    }

    signal(SIGINT,  handleSignal);
    signal(SIGTERM, handleSignal);

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║   FlashBook — UDP Receiver + SPSC Queue      ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    printf("Port: %u   Queue capacity: %zu   Order size: %zu bytes\n\n",
           port, g_queue.capacity(), sizeof(Order));
    fflush(stdout);

    std::thread consumer(consumerThread);

    UDPReceiver receiver(port);
    g_receiver = &receiver;

    receiver.start([](const Order& o) {
        printOrder("RECV", o);  

        if (!g_queue.push(o)) {
            LOG("WARN", "Queue full! Dropping order id=%llu",
                (unsigned long long)o.order_id);
        }
    });

    g_shutdown.store(true);
    consumer.join();

    printf("\n[DONE] recv=%llu  dropped=%llu\n",
           (unsigned long long)receiver.receivedCount(),
           (unsigned long long)receiver.droppedCount());

    return 0;
}
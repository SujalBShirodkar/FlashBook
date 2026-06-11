#include "udp_receiver.h"
#include "engine.h"
#include "spsc_queue.h"
#include "order.h"
#include "logger.h"
#include <csignal>
#include <cstdio>
#include <atomic>
#include <pthread.h>
#include <unistd.h>
#include <cstring>

static OrderQueue    g_queue;
static UDPReceiver*  g_receiver = nullptr;
static Engine*       g_engine   = nullptr;

void handleSignal(int sig) {
    fprintf(stderr, "\n[SIGNAL] Caught %d — shutting down...\n", sig);
    if (g_receiver) g_receiver->stop();
    if (g_engine)   g_engine->stop();
}

static void pinThreadToCore(int core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    int ret = pthread_setaffinity_np(
        pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        LOG("WARN", "Could not pin main thread to core %d: %s",
            core, strerror(ret));
    } else {
        LOG("INIT", "Network thread pinned to core %d", core);
    }
}

void logRecv(const Order& o) {
    LOG("RECV", "id=%-6llu %-4s %-7s qty=%-6u price=%s",
        (unsigned long long)o.order_id,
        sideToString(o.side),
        typeToString(o.type),
        o.quantity,
        priceToString(o.price).c_str());
}

int main(int argc, char* argv[]) {
    uint16_t port         = 9001;
    int      net_core     = 0;  
    int      engine_core  = 1;   
    bool     pin_cores    = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--pin") {
            pin_cores = true;
        } else {
            int p = atoi(argv[i]);
            if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
        }
    }

    signal(SIGINT,  handleSignal);
    signal(SIGTERM, handleSignal);

    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║         FlashBook — Matching Engine v0.6         ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");
    printf("Port        : %u\n", port);
    printf("Queue cap   : %zu orders\n", g_queue.capacity());
    printf("Order size  : %zu bytes\n", sizeof(Order));
    printf("CPU pinning : %s\n", pin_cores ? "ON" : "OFF (pass --pin to enable)");
    printf("Press Ctrl+C to stop.\n\n");
    fflush(stdout);

    Engine engine(g_queue);
    g_engine = &engine;

    if (pin_cores) {
        engine.setCpuAffinity(engine_core);
    }
    engine.start();

    if (pin_cores) {
        pinThreadToCore(net_core);
    }

    UDPReceiver receiver(port);
    g_receiver = &receiver;

    receiver.start([&](const Order& o) {
        logRecv(o);

        if (!g_queue.push(o)) {
            LOG("WARN", "Queue full! Dropping order id=%llu",
                (unsigned long long)o.order_id);
        }
    });

    engine.stop();
    engine.join();

    printf("\n╔══════════════════════════════════════════════════╗\n");
    printf("║                   Session Summary                ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Orders received : %-6llu                        ║\n",
           (unsigned long long)receiver.receivedCount());
    printf("║  Orders dropped  : %-6llu                        ║\n",
           (unsigned long long)receiver.droppedCount());
    printf("║  Orders processed: %-6llu                        ║\n",
           (unsigned long long)engine.ordersProcessed());
    printf("║  Trades executed : %-6llu                        ║\n",
           (unsigned long long)engine.tradesExecuted());
    printf("╚══════════════════════════════════════════════════╝\n");

    engine.printBook();
    engine.printLatency();

    return 0;
}
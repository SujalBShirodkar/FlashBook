#include "udp_receiver.h"
#include "order.h"
#include "logger.h"
#include <csignal>
#include <cstdio>
#include <atomic>

static UDPReceiver* g_receiver = nullptr;
static std::atomic<bool> g_shutdown{false};

void handleSignal(int sig) {
    fprintf(stderr, "\n[SIGNAL] Caught signal %d — shutting down...\n", sig);
    g_shutdown.store(true);
    if (g_receiver) g_receiver->stop();
}

void printOrder(const Order& o) {
    printf("[RECV]  id=%-6llu  %-6s  %-7s  qty=%-6u  price=%s\n",
           (unsigned long long)o.order_id,
           sideToString(o.side),
           typeToString(o.type),
           o.quantity,
           priceToString(o.price).c_str());
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    uint16_t port = 9001;

    if (argc >= 2) {
        int p = atoi(argv[1]);
        if (p > 0 && p < 65536) {
            port = static_cast<uint16_t>(p);
        } else {
            fprintf(stderr, "Invalid port. Using default 9001.\n");
        }
    }

    signal(SIGINT,  handleSignal);
    signal(SIGTERM, handleSignal);

    printf("╔════════════════════════════════════════╗\n");
    printf("║        FlashBook — Order Receiver      ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("Listening on UDP port %u\n", port);
    printf("Press Ctrl+C to stop.\n\n");
    printf("%-8s %-8s %-8s %-8s %-8s %s\n",
           "ID", "SIDE", "TYPE", "QTY", "PRICE", "");
    printf("%-8s %-8s %-8s %-8s %-8s\n",
           "--------","--------","--------","--------","--------");
    fflush(stdout);

    UDPReceiver receiver(port);
    g_receiver = &receiver;

    receiver.start([](const Order& o) {
        printOrder(o);
    });

    printf("\n[DONE] Total received: %llu  dropped: %llu\n",
           (unsigned long long)receiver.receivedCount(),
           (unsigned long long)receiver.droppedCount());

    return 0;
}

#include "order.h"
#include "logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>

static int          g_sock = -1;
static sockaddr_in  g_addr{};
static uint64_t     g_order_id = 1;
static uint64_t     g_sent     = 0;

static void initSocket(const char* host, uint16_t port) {
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock < 0) { perror("socket"); exit(1); }
    g_addr.sin_family = AF_INET;
    g_addr.sin_port   = htons(port);
    inet_pton(AF_INET, host, &g_addr.sin_addr);
}

static void sendOrder(const Order& o) {
    sendto(g_sock, &o, sizeof(o), 0,
           reinterpret_cast<sockaddr*>(&g_addr), sizeof(g_addr));
    ++g_sent;
}

static uint64_t parsePrice(const std::string& s) {
    double d = std::stod(s);
    return static_cast<uint64_t>(d * 10000.0 + 0.5);
}

static Order makeOrder(uint64_t id, uint64_t price, uint32_t qty,
                       Side side, OrderType type = OrderType::LIMIT) {
    Order o{};
    o.order_id = id;
    o.price    = price;
    o.recv_tsc = 0;
    o.quantity = qty;
    o.side     = side;
    o.type     = type;
    return o;
}

static void printBanner(const char* host, uint16_t port) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║         FlashBook — Interactive CLI              ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Target: %-38s  ║\n",
           (std::string(host) + ":" + std::to_string(port)).c_str());
    printf("╚══════════════════════════════════════════════════╝\n");
    printf("Type 'help' for commands.\n\n");
}

static void printHelp() {
    printf("\n  Commands:\n");
    printf("  ─────────────────────────────────────────────\n");
    printf("  buy  <price> <qty>      Place a BUY limit order\n");
    printf("  sell <price> <qty>      Place a SELL limit order\n");
    printf("  cancel <order_id>       Cancel an order by ID\n");
    printf("  burst <n>               Send N alternating BUY/SELL\n");
    printf("  burst <n> <price>       Burst at a fixed price\n");
    printf("  replay <file>           Replay orders from a file\n");
    printf("  stats                   Show session stats\n");
    printf("  help                    Show this message\n");
    printf("  quit / exit             Exit the CLI\n");
    printf("  ─────────────────────────────────────────────\n");
    printf("  Price format: decimal  e.g. 180.25\n");
    printf("  Price*10000 also accepted e.g. 1802500\n\n");
}

static void printStats() {
    printf("\n  Session stats:\n");
    printf("  Orders sent : %llu\n", (unsigned long long)g_sent);
    printf("  Next ID     : %llu\n", (unsigned long long)g_order_id);
    printf("\n");
}

static bool processCommand(const std::string& line) {
    if (line.empty() || line[0] == '#') return true;

    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "quit" || cmd == "exit") return false;

    if (cmd == "help") { printHelp(); return true; }
    if (cmd == "stats") { printStats(); return true; }

    if (cmd == "buy" || cmd == "sell") {
        std::string price_str;
        uint32_t qty = 0;
        if (!(ss >> price_str >> qty)) {
            printf("  Usage: %s <price> <qty>   e.g. buy 180.25 100\n",
                   cmd.c_str());
            return true;
        }

        uint64_t price;
        try {
            price = (price_str.find('.') != std::string::npos)
                  ? parsePrice(price_str)
                  : std::stoull(price_str);
        } catch (...) {
            printf("  Invalid price: %s\n", price_str.c_str());
            return true;
        }

        Side side = (cmd == "buy") ? Side::BUY : Side::SELL;
        Order o   = makeOrder(g_order_id++, price, qty, side);
        sendOrder(o);

        printf("  → Sent %-4s  id=%-4llu  qty=%-6u  price=%s\n",
               sideToString(side),
               (unsigned long long)(g_order_id - 1),
               qty,
               priceToString(price).c_str());
        return true;
    }

    if (cmd == "cancel") {
        uint64_t id = 0;
        if (!(ss >> id)) {
            printf("  Usage: cancel <order_id>\n");
            return true;
        }
        Order o = makeOrder(id, 0, 0, Side::BUY, OrderType::CANCEL);
        sendOrder(o);
        printf("  → Sent CANCEL for id=%llu\n", (unsigned long long)id);
        return true;
    }

    if (cmd == "burst") {
        int n = 0;
        uint64_t price = 1800000;
        std::string tok;
        if (!(ss >> n)) {
            printf("  Usage: burst <count> [price]\n");
            return true;
        }
        if (ss >> tok) {
            try {
                price = (tok.find('.') != std::string::npos)
                      ? parsePrice(tok)
                      : std::stoull(tok);
            } catch (...) {}
        }

        printf("  → Burst sending %d orders at price %s...\n",
               n, priceToString(price).c_str());

        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; ++i) {
            Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
            uint64_t p = price + (uint64_t)(i % 5) * 100;
            Order o = makeOrder(g_order_id++, p, 10, side);
            sendOrder(o);
        }
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1-t0).count();
        printf("  → Done. %d orders in %.2f ms (%.0f orders/sec)\n",
               n, ms, n / (ms / 1000.0));
        return true;
    }

    if (cmd == "replay") {
        std::string filename;
        if (!(ss >> filename)) {
            printf("  Usage: replay <filename>\n");
            return true;
        }
        std::ifstream f(filename);
        if (!f.is_open()) {
            printf("  Cannot open file: %s\n", filename.c_str());
            return true;
        }

        printf("  → Replaying: %s\n", filename.c_str());
        std::string fline;
        int count = 0;
        while (std::getline(f, fline)) {
            while (!fline.empty() && isspace(fline.back())) fline.pop_back();
            if (fline.empty() || fline[0] == '#') continue;

            std::istringstream rs(fline);
            std::string rcmd; rs >> rcmd;
            std::transform(rcmd.begin(), rcmd.end(), rcmd.begin(), ::tolower);

            if (rcmd == "buy" || rcmd == "sell") {
                uint64_t id, price; uint32_t qty;
                if (rs >> id >> price >> qty) {
                    Side side = (rcmd == "buy") ? Side::BUY : Side::SELL;
                    Order o = makeOrder(id, price, qty, side);
                    g_order_id = std::max(g_order_id, id + 1);
                    sendOrder(o);
                    printf("  [%4d] %-4s id=%-4llu price=%s qty=%u\n",
                           ++count, sideToString(side),
                           (unsigned long long)id,
                           priceToString(price).c_str(), qty);
                }
            } else if (rcmd == "cancel") {
                uint64_t id; rs >> id;
                Order o = makeOrder(id, 0, 0, Side::BUY, OrderType::CANCEL);
                sendOrder(o);
                printf("  [%4d] CANCEL id=%llu\n",
                       ++count, (unsigned long long)id);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        printf("  → Replay done. %d orders sent.\n\n", count);
        return true;
    }

    printf("  Unknown command: '%s'  (type 'help')\n", cmd.c_str());
    return true;
}

int main(int argc, char* argv[]) {
    const char* host = "127.0.0.1";
    uint16_t    port = 9001;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = static_cast<uint16_t>(atoi(argv[2]));

    initSocket(host, port);
    printBanner(host, port);

    std::string line;
    while (true) {
        printf("flashbook> ");
        fflush(stdout);
        if (!std::getline(std::cin, line)) break;
        while (!line.empty() && isspace(line.back())) line.pop_back();
        if (!processCommand(line)) break;
    }

    printf("\nSession ended. Orders sent: %llu\n",
           (unsigned long long)g_sent);
    close(g_sock);
    return 0;
}
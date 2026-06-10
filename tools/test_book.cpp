#include "order_book.h"
#include "order.h"
#include "logger.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cstdio>

void onTrade(const Trade& t) {
    printf("\n  *** TRADE EXECUTED ***\n");
    printf("  buy_id=%-6llu  sell_id=%-6llu\n",
           (unsigned long long)t.buy_order_id,
           (unsigned long long)t.sell_order_id);
    printf("  price=%-12s  qty=%u\n",
           priceToString(t.price).c_str(), t.quantity);
    printf("  **********************\n\n");
    fflush(stdout);
}

bool processLine(const std::string& line, OrderBook& book) {
    if (line.empty() || line[0] == '#') return true; 

    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;

    for (auto& c : cmd) c = tolower(c);

    if (cmd == "quit" || cmd == "exit") {
        return false;
    }

    if (cmd == "print") {
        book.prettyPrint();
        return true;
    }

    if (cmd == "match") {
        int n = book.match();
        printf("[MATCH] %d trade(s) executed\n", n);
        return true;
    }

    if (cmd == "cancel") {
        uint64_t id;
        if (!(ss >> id)) { printf("Usage: cancel <order_id>\n"); return true; }
        book.cancelOrder(id);
        return true;
    }

    if (cmd == "buy" || cmd == "sell") {
        uint64_t id, price;
        uint32_t qty;
        if (!(ss >> id >> price >> qty)) {
            printf("Usage: %s <id> <price*10000> <qty>\n", cmd.c_str());
            printf("  e.g. buy 1 1802500 100   (means $180.25)\n");
            return true;
        }

        Order o{};
        o.order_id = id;
        o.price    = price;
        o.quantity = qty;
        o.side     = (cmd == "buy") ? Side::BUY : Side::SELL;
        o.type     = OrderType::LIMIT;

        book.addAndMatch(o);
        return true;
    }

    if (cmd == "replay") {
        std::string filename;
        if (!(ss >> filename)) {
            printf("Usage: replay <filename>\n");
            return true;
        }

        std::ifstream file(filename);
        if (!file.is_open()) {
            printf("[ERROR] Cannot open file: %s\n", filename.c_str());
            return true;
        }

        printf("[REPLAY] Replaying from: %s\n", filename.c_str());
        std::string fline;
        int count = 0;
        while (std::getline(file, fline)) {
            if (!fline.empty() && fline[0] != '#') {
                printf("  >> %s\n", fline.c_str());
                processLine(fline, book);
                ++count;
            }
        }
        printf("[REPLAY] Done. %d commands replayed.\n", count);
        book.prettyPrint();
        return true;
    }

    printf("[WARN] Unknown command: '%s'\n", cmd.c_str());
    printf("Commands: buy, sell, cancel, print, match, replay, quit\n");
    return true;
}

int main(int argc, char* argv[]) {
    OrderBook book(onTrade);

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║      FlashBook — Order Book Test Harness     ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    printf("Commands: buy <id> <price*10000> <qty>\n");
    printf("          sell <id> <price*10000> <qty>\n");
    printf("          cancel <id>\n");
    printf("          print | match | replay <file> | quit\n\n");

    if (argc >= 2) {
        std::string cmd = "replay ";
        cmd += argv[1];
        processLine(cmd, book);
        return 0;
    }

    std::string line;
    while (true) {
        printf("> ");
        fflush(stdout);
        if (!std::getline(std::cin, line)) break;
        if (!processLine(line, book)) break;
    }

    printf("Goodbye.\n");
    return 0;
}
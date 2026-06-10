#include "order.h"
#include "logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <cctype>

static int createSocket() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }
    return fd;
}

static sockaddr_in makeAddr(const char* host, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid host: %s\n", host);
        exit(1);
    }
    return addr;
}

static void sendOrder(int fd, const sockaddr_in& addr, const Order& o) {
    ssize_t sent = sendto(
        fd,
        &o,
        sizeof(o),
        0,
        reinterpret_cast<const sockaddr*>(&addr),
        sizeof(addr)
    );
    if (sent < 0) {
        perror("sendto");
        exit(1);
    }
}

static std::string toUpper(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = toupper(c);
    return r;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr,
            "Usage:\n"
            "  Single:  %s <host> <port> BUY|SELL <price*10000> <qty> [id]\n"
            "  Cancel:  %s <host> <port> CANCEL <order_id>\n"
            "  Burst:   %s <host> <port> burst <count>\n"
            "\nExamples:\n"
            "  %s 127.0.0.1 9001 BUY  1802500 100\n"
            "  %s 127.0.0.1 9001 SELL 1799500  50\n"
            "  %s 127.0.0.1 9001 burst 500\n",
            argv[0], argv[0], argv[0],
            argv[0], argv[0], argv[0]);
        return 1;
    }

    const char*  host = argv[1];
    uint16_t     port = static_cast<uint16_t>(atoi(argv[2]));
    std::string  cmd  = toUpper(argv[3]);

    int          fd   = createSocket();
    sockaddr_in  addr = makeAddr(host, port);

    static uint64_t id_counter = 1;

    if (cmd == "BURST") {
        if (argc < 5) {
            fprintf(stderr, "burst mode requires <count>\n");
            return 1;
        }
        int count = atoi(argv[4]);
        printf("Sending %d orders in burst mode...\n", count);

        for (int i = 0; i < count; ++i) {
            Order o{};
            o.order_id = id_counter++;
            o.price    = 1800000 + (uint64_t)(i % 10) * 500; 
            o.quantity = 100;
            o.side     = (i % 2 == 0) ? Side::BUY : Side::SELL;
            o.type     = OrderType::LIMIT;

            sendOrder(fd, addr, o);
        }

        printf("Burst complete: %d orders sent.\n", count);
        close(fd);
        return 0;
    }

    if (cmd == "CANCEL") {
        if (argc < 5) {
            fprintf(stderr, "CANCEL requires <order_id>\n");
            return 1;
        }
        Order o{};
        o.order_id = static_cast<uint64_t>(atoll(argv[4]));
        o.price    = 0;
        o.quantity = 0;
        o.side     = Side::BUY;   
        o.type     = OrderType::CANCEL;

        sendOrder(fd, addr, o);
        printf("Sent CANCEL for order_id=%llu\n",
               (unsigned long long)o.order_id);
        close(fd);
        return 0;
    }

    if (argc < 6) {
        fprintf(stderr, "Single order requires <side> <price*10000> <qty>\n");
        return 1;
    }

    Side side;
    if (cmd == "BUY")       side = Side::BUY;
    else if (cmd == "SELL") side = Side::SELL;
    else {
        fprintf(stderr, "Unknown side: %s (expected BUY or SELL)\n", argv[3]);
        return 1;
    }

    Order o{};
    o.order_id = (argc >= 7) ? static_cast<uint64_t>(atoll(argv[6])) : id_counter++;
    o.price    = static_cast<uint64_t>(atoll(argv[4]));
    o.quantity = static_cast<uint32_t>(atoi(argv[5]));
    o.side     = side;
    o.type     = OrderType::LIMIT;

    sendOrder(fd, addr, o);

    printf("Sent %-4s  id=%-4llu  qty=%-6u  price=%s\n",
           sideToString(o.side),
           (unsigned long long)o.order_id,
           o.quantity,
           priceToString(o.price).c_str());

    close(fd);
    return 0;
}
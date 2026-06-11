#include "udp_receiver.h"
#include "latency.h"
#include "logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

UDPReceiver::UDPReceiver(uint16_t port)
    : port_(port)
{}

UDPReceiver::~UDPReceiver() {
    if (sock_fd_ >= 0) {
        close(sock_fd_);
        sock_fd_ = -1;
    }
}

void UDPReceiver::createAndBindSocket() {
    sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd_ < 0) {
        throw std::runtime_error(
            std::string("socket() failed: ") + strerror(errno));
    }

    int opt = 1;
    if (setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        throw std::runtime_error(
            std::string("setsockopt(SO_REUSEADDR) failed: ") + strerror(errno));
    }

    int flags = fcntl(sock_fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(sock_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error(
            std::string("fcntl(O_NONBLOCK) failed: ") + strerror(errno));
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port_);   
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error(
            std::string("bind() failed on port ") + std::to_string(port_)
            + ": " + strerror(errno));
    }

    LOG("INIT", "UDP socket bound on port %u (non-blocking)", port_);
}

void UDPReceiver::start(OrderCallback cb) {
    createAndBindSocket();
    running_.store(true);

    alignas(alignof(Order)) uint8_t buf[sizeof(Order)];

    sockaddr_in sender{};
    socklen_t   sender_len = sizeof(sender);

    LOG("RECV", "Starting busy-poll receive loop on port %u", port_);

    while (running_.load(std::memory_order_relaxed)) {
        ssize_t n = recvfrom(
            sock_fd_,
            buf,
            sizeof(buf),
            0,                                        
            reinterpret_cast<sockaddr*>(&sender),
            &sender_len
        );

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            LOG("ERROR", "recvfrom() error: %s", strerror(errno));
            break;
        }

        if (static_cast<size_t>(n) != sizeof(Order)) {
            drop_count_.fetch_add(1, std::memory_order_relaxed);
            LOG("WARN", "Dropped packet: expected %zu bytes, got %zd",
                sizeof(Order), n);
            continue;
        }

        uint64_t tsc_now = rdtsc();
        Order& order = *reinterpret_cast<Order*>(buf);
        order.recv_tsc = tsc_now;

        recv_count_.fetch_add(1, std::memory_order_relaxed);
        cb(order);
    }

    LOG("RECV", "Receive loop stopped. received=%llu dropped=%llu",
        (unsigned long long)recv_count_.load(),
        (unsigned long long)drop_count_.load());
}

void UDPReceiver::stop() {
    running_.store(false, std::memory_order_relaxed);
}
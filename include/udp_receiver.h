#pragma once
#include "order.h"
#include <functional>
#include <cstdint>
#include <atomic>

class UDPReceiver {
public:
    using OrderCallback = std::function<void(const Order&)>;

    explicit UDPReceiver(uint16_t port);
    ~UDPReceiver();

    void start(OrderCallback cb);

    void stop();

    uint64_t receivedCount() const { return recv_count_.load(); }

    uint64_t droppedCount() const { return drop_count_.load(); }

private:
    void createAndBindSocket();

    int                   sock_fd_{-1};
    uint16_t              port_;
    std::atomic<bool>     running_{false};
    std::atomic<uint64_t> recv_count_{0};
    std::atomic<uint64_t> drop_count_{0};
};
#pragma once
#include "order.h"
#include <map>
#include <unordered_map>
#include <deque>
#include <cstdint>
#include <optional>
#include <functional>

struct PriceLevel {
    uint64_t         price;
    std::deque<Order> orders;
    uint32_t         total_qty{0};

    void addOrder(const Order& o) {
        orders.push_back(o);
        total_qty += o.quantity;
    }

    void removeOrder(uint64_t order_id) {
        for (auto it = orders.begin(); it != orders.end(); ++it) {
            if (it->order_id == order_id) {
                total_qty -= it->quantity;
                orders.erase(it);
                return;
            }
        }
    }

    bool empty() const { return orders.empty(); }
};

struct Trade {
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    uint64_t price;       
    uint32_t quantity;    
    uint64_t timestamp_us;
};

class OrderBook {
public:
    using TradeCallback = std::function<void(const Trade&)>;

    explicit OrderBook(TradeCallback on_trade)
        : on_trade_(std::move(on_trade)) {}

    void addOrder(const Order& o);

    bool cancelOrder(uint64_t order_id);

    int match();

    int addAndMatch(const Order& o);

    void prettyPrint() const;

    size_t bidLevels()  const { return bids_.size(); }
    size_t askLevels()  const { return asks_.size(); }
    size_t totalOrders() const { return order_index_.size(); }

    uint64_t bestBid() const;
    uint64_t bestAsk() const;

private:
    std::map<uint64_t, PriceLevel, std::greater<uint64_t>> bids_;
    std::map<uint64_t, PriceLevel, std::less<uint64_t>>    asks_;

    struct OrderLocation { Side side; uint64_t price; };
    std::unordered_map<uint64_t, OrderLocation> order_index_;

    TradeCallback on_trade_;
    uint64_t      trade_count_{0};
};
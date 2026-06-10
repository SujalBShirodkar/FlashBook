#include "order_book.h"
#include "logger.h"
#include <cstdio>

void OrderBook::addOrder(const Order& o) {
    if (o.type == OrderType::CANCEL) {
        cancelOrder(o.order_id);
        return;
    }

    order_index_[o.order_id] = { o.side, o.price };

    if (o.side == Side::BUY) {
        bids_[o.price].price = o.price;
        bids_[o.price].addOrder(o);
        LOG("BOOK", "ADD BUY  id=%-6llu qty=%-6u price=%s",
            (unsigned long long)o.order_id,
            o.quantity,
            priceToString(o.price).c_str());
    } else {
        asks_[o.price].price = o.price;
        asks_[o.price].addOrder(o);
        LOG("BOOK", "ADD SELL id=%-6llu qty=%-6u price=%s",
            (unsigned long long)o.order_id,
            o.quantity,
            priceToString(o.price).c_str());
    }
}

bool OrderBook::cancelOrder(uint64_t order_id) {
    auto it = order_index_.find(order_id);
    if (it == order_index_.end()) {
        LOG("WARN", "CANCEL failed: id=%llu not found",
            (unsigned long long)order_id);
        return false;
    }

    auto [side, price] = it->second;
    order_index_.erase(it);

    if (side == Side::BUY) {
        auto bit = bids_.find(price);
        if (bit != bids_.end()) {
            bit->second.removeOrder(order_id);
            if (bit->second.empty()) bids_.erase(bit);
        }
    } else {
        auto ait = asks_.find(price);
        if (ait != asks_.end()) {
            ait->second.removeOrder(order_id);
            if (ait->second.empty()) asks_.erase(ait);
        }
    }

    LOG("BOOK", "CANCEL id=%llu", (unsigned long long)order_id);
    return true;
}

int OrderBook::match() {
    int trades = 0;

    while (!bids_.empty() && !asks_.empty()) {
        auto& best_bid_level = bids_.begin()->second;
        auto& best_ask_level = asks_.begin()->second;

        if (best_bid_level.price < best_ask_level.price)
            break;

        Order& bid = best_bid_level.orders.front();
        Order& ask = best_ask_level.orders.front();

        uint32_t fill_qty = std::min(bid.quantity, ask.quantity);

        Trade trade{};
        trade.buy_order_id  = bid.order_id;
        trade.sell_order_id = ask.order_id;
        trade.price         = ask.price;
        trade.quantity      = fill_qty;
        trade.timestamp_us  = nowUs();

        LOG("TRADE",
            "MATCH buy_id=%-4llu sell_id=%-4llu qty=%-6u price=%s",
            (unsigned long long)trade.buy_order_id,
            (unsigned long long)trade.sell_order_id,
            trade.quantity,
            priceToString(trade.price).c_str());

        on_trade_(trade);
        ++trades;
        ++trade_count_;

        bid.quantity -= fill_qty;
        ask.quantity -= fill_qty;
        best_bid_level.total_qty -= fill_qty;
        best_ask_level.total_qty -= fill_qty;

        if (bid.quantity == 0) {
            order_index_.erase(bid.order_id);
            best_bid_level.orders.pop_front();
        }
        if (ask.quantity == 0) {
            order_index_.erase(ask.order_id);
            best_ask_level.orders.pop_front();
        }

        if (best_bid_level.empty()) bids_.erase(bids_.begin());
        if (best_ask_level.empty()) asks_.erase(asks_.begin());
    }

    return trades;
}

int OrderBook::addAndMatch(const Order& o) {
    addOrder(o);
    return match();
}

uint64_t OrderBook::bestBid() const {
    return bids_.empty() ? 0 : bids_.begin()->first;
}

uint64_t OrderBook::bestAsk() const {
    return asks_.empty() ? 0 : asks_.begin()->first;
}

void OrderBook::prettyPrint() const {
    const int W = 52;
    auto line = [&]{ printf("  %s\n", std::string(W, '-').c_str()); };

    printf("\n");
    line();
    printf("  %-*s\n", W, "  ORDER BOOK");
    line();

    if (asks_.empty()) {
        printf("  %-*s\n", W, "  (no asks)");
    } else {
        printf("  %-14s %-12s %-8s %s\n", "  ASKS price", "total qty", "orders", "");
        for (auto it = asks_.rbegin(); it != asks_.rend(); ++it) {
            const auto& lvl = it->second;
            bool is_best = (it == asks_.rbegin());
            printf("  %-14s %-12u %-8zu %s\n",
                   priceToString(lvl.price).c_str(),
                   lvl.total_qty,
                   lvl.orders.size(),
                   is_best ? "<-- best ask" : "");
        }
    }

    if (!bids_.empty() && !asks_.empty()) {
        uint64_t spread = bestAsk() - bestBid();
        printf("  %s SPREAD: %s %s\n",
               std::string(8,'-').c_str(),
               priceToString(spread).c_str(),
               std::string(8,'-').c_str());
    } else {
        printf("  %s\n", std::string(W, '-').c_str());
    }

    if (bids_.empty()) {
        printf("  %-*s\n", W, "  (no bids)");
    } else {
        printf("  %-14s %-12s %-8s %s\n", "  BIDS price", "total qty", "orders", "");
        for (auto it = bids_.begin(); it != bids_.end(); ++it) {
            const auto& lvl = it->second;
            bool is_best = (it == bids_.begin());
            printf("  %-14s %-12u %-8zu %s\n",
                   priceToString(lvl.price).c_str(),
                   lvl.total_qty,
                   lvl.orders.size(),
                   is_best ? "<-- best bid" : "");
        }
    }

    line();
    printf("  Bid levels: %zu   Ask levels: %zu   Total orders: %zu   Trades: %llu\n",
           bidLevels(), askLevels(), totalOrders(),
           (unsigned long long)trade_count_);
    line();
    printf("\n");
}
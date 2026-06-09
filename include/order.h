#pragma once
#include <cstdint>
#include<string>

enum class Side : uint8_t{
    BUY=0,
    SELL=1
};

enum class OrderType : uint8_t{
    LIMIT=0,
    CANCEL=1
};

struct alignas(8) Order{
    uint64_t order_id;
    uint64_t price;
    uint32_t quantity;
    Side side;
    OrderType type;
    uint8_t _pad[2];
};

static_assert(sizeof(Order) == 24, "Order struct must be exactly 24 bytes");
static_assert(alignof(Order) == 8, "Order struct must be 8-byte aligned");

inline std::string priceToString(uint64_t price) {
    uint64_t whole = price / 10000;
    uint64_t frac  = price % 10000;
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu.%04llu", (unsigned long long)whole, (unsigned long long)frac);
    return buf;
}

inline const char* sideToString(Side s) {
    return s == Side::BUY ? "BUY" : "SELL";
}

inline const char* typeToString(OrderType t) {
    return t == OrderType::LIMIT ? "LIMIT" : "CANCEL";
}

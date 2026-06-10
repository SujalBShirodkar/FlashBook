#pragma once
#include <cstdio>
#include <ctime>
#include <cstdint>

inline uint64_t nowUs() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1'000ULL;
}

inline void printTimestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm t;
    localtime_r(&ts.tv_sec, &t);
    fprintf(stderr, "[%02d:%02d:%02d.%06ld] ", t.tm_hour, t.tm_min, t.tm_sec, ts.tv_nsec / 1000L);
}

#define LOG(tag, fmt, ...)                                       \
    do {                                                         \
        printTimestamp();                                        \
        fprintf(stderr, "[%-6s] " fmt "\n", tag, ##__VA_ARGS__); \
    } while(0)
    
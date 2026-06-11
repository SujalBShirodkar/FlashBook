#pragma once
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>
#include <numeric>
#include <time.h>

inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ volatile (
        "rdtsc"
        : "=a"(lo), "=d"(hi)
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

inline uint64_t rdtscp() {
    uint32_t lo, hi, aux;
    __asm__ volatile (
        "rdtscp"
        : "=a"(lo), "=d"(hi), "=c"(aux)
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

class TscClock {
public:
    TscClock() {
        calibrate();
    }

    double ticksToNs(uint64_t ticks) const {
        return static_cast<double>(ticks) * ns_per_tick_;
    }

    double ticksToUs(uint64_t ticks) const {
        return ticksToNs(ticks) / 1000.0;
    }

    double tscFreqGHz() const { return tsc_freq_ghz_; }

private:
    void calibrate() {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        uint64_t tsc0 = rdtsc();

        struct timespec now;
        do {
            clock_gettime(CLOCK_MONOTONIC, &now);
        } while ((now.tv_sec  - t0.tv_sec)  * 1'000'000'000LL +
                 (now.tv_nsec - t0.tv_nsec) < 100'000'000LL);

        uint64_t tsc1 = rdtscp();
        clock_gettime(CLOCK_MONOTONIC, &t1);

        uint64_t ns_elapsed =
            (t1.tv_sec  - t0.tv_sec)  * 1'000'000'000ULL +
            (t1.tv_nsec - t0.tv_nsec);

        uint64_t tsc_elapsed = tsc1 - tsc0;

        ns_per_tick_  = static_cast<double>(ns_elapsed) / tsc_elapsed;
        tsc_freq_ghz_ = static_cast<double>(tsc_elapsed) / ns_elapsed;

        fprintf(stderr,
            "[TSC  ] Calibrated: %.3f GHz  (%.4f ns/tick)\n",
            tsc_freq_ghz_, ns_per_tick_);
    }

    double ns_per_tick_{1.0};
    double tsc_freq_ghz_{1.0};
};

inline TscClock& globalClock() {
    static TscClock clk;
    return clk;
}

template<size_t MAX_SAMPLES = 1'000'000>
class LatencyStats {
public:
    void record(double ns) {
        if (count_ < MAX_SAMPLES) {
            samples_[count_++] = ns;
        }
        if (ns < min_ns_) min_ns_ = ns;
        if (ns > max_ns_) max_ns_ = ns;
        sum_ns_ += ns;
    }

    void report(const char* label = "Latency") const {
        if (count_ == 0) {
            printf("[%s] No samples recorded.\n", label);
            return;
        }

        std::vector<double> sorted(samples_.begin(), samples_.begin() + count_);
        std::sort(sorted.begin(), sorted.end());

        auto pct = [&](double p) -> double {
            size_t idx = static_cast<size_t>(p / 100.0 * (count_ - 1));
            return sorted[idx];
        };

        printf("\n  ┌─────────────────────────────────────────┐\n");
        printf("  │  %-39s│\n", label);
        printf("  ├─────────────────────────────────────────┤\n");
        printf("  │  Samples  : %-27zu  │\n", count_);
        printf("  │  Min      : %-24.1f ns  │\n", min_ns_);
        printf("  │  Mean     : %-24.1f ns  │\n", sum_ns_ / count_);
        printf("  │  p50      : %-24.1f ns  │\n", pct(50));
        printf("  │  p90      : %-24.1f ns  │\n", pct(90));
        printf("  │  p99      : %-24.1f ns  │\n", pct(99));
        printf("  │  p99.9    : %-24.1f ns  │\n", pct(99.9));
        printf("  │  Max      : %-24.1f ns  │\n", max_ns_);
        printf("  └─────────────────────────────────────────┘\n\n");
        fflush(stdout);
    }

    size_t count() const { return count_; }

private:
    std::array<double, MAX_SAMPLES> samples_{};
    size_t count_{0};
    double min_ns_{1e18};
    double max_ns_{0.0};
    double sum_ns_{0.0};
};
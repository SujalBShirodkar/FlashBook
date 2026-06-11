#pragma once
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>

inline bool setRealtimeScheduling(int priority = 80) {
    struct sched_param sp{};
    sp.sched_priority = priority;
    int ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (ret != 0) {
        fprintf(stderr,
            "[TUNE] SCHED_FIFO failed (need sudo or CAP_SYS_NICE): %s\n",
            strerror(ret));
        return false;
    }
    fprintf(stderr, "[TUNE] SCHED_FIFO priority=%d applied\n", priority);
    return true;
}

inline bool lockMemory() {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        fprintf(stderr,
            "[TUNE] mlockall failed: %s\n", strerror(errno));
        return false;
    }
    fprintf(stderr, "[TUNE] mlockall: all pages locked in RAM\n");
    return true;
}

inline void printCpuTuningInstructions() {
    fprintf(stderr,
        "[TUNE] For lowest latency on bare metal, run:\n"
        "       sudo cpupower frequency-set -g performance\n"
        "       sudo sh -c 'echo 0 > /proc/sys/kernel/nmi_watchdog'\n"
        "       sudo sh -c 'echo 1 > /proc/sys/kernel/numa_balancing'\n"
        "       taskset -c 0 ./flashbook   # CPU pinning via taskset\n");
}

inline void prefetch(const void* addr) {
    __builtin_prefetch(addr, 0, 3);
}

#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#define FORCE_INLINE __attribute__((always_inline)) inline

static constexpr int HW_CACHE_LINE = 64;

inline void printSystemInfo() {
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    long page_size = sysconf(_SC_PAGESIZE);

    fprintf(stderr, "[SYS ] Cores online : %ld\n", num_cores);
    fprintf(stderr, "[SYS ] Page size    : %ld bytes\n", page_size);
    fprintf(stderr, "[SYS ] Cache line   : %d bytes\n", HW_CACHE_LINE);

    FILE* f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[256];
        bool found_const = false, found_nonstop = false;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "constant_tsc"))  found_const   = true;
            if (strstr(line, "nonstop_tsc"))   found_nonstop = true;
        }
        fclose(f);
        fprintf(stderr, "[SYS ] constant_tsc : %s\n",
                found_const   ? "YES (TSC safe for timing)" : "NO");
        fprintf(stderr, "[SYS ] nonstop_tsc  : %s\n",
                found_nonstop ? "YES" : "NO");
    }
}
# FlashBook

> **A two-threaded, lock-free, zero-malloc electronic trading system** — receives buy/sell orders over UDP, matches them using price-time priority, and measures the latency of every order with nanosecond precision.

Built entirely in **C++17** with no external dependencies beyond the standard library and pthreads.

---

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [System Design](#system-design)
  - [Order Struct](#order-struct--the-atom-of-the-system)
  - [SPSC Ring Buffer](#spsc-ring-buffer--the-bridge-between-threads)
  - [Limit Order Book](#limit-order-book--the-core-business-logic)
  - [Memory Pool](#memory-pool--zero-runtime-allocation)
  - [Latency Measurement](#latency-measurement--tsc-hardware-timer)
  - [UDP Receiver](#udp-receiver--the-network-layer)
  - [Linux Performance Tuning](#linux-performance-tuning)
- [Project Structure](#project-structure)
- [Building](#building)
- [Running](#running)
- [Tools Reference](#tools-reference)
- [Benchmark Results](#benchmark-results)
- [Known Limitations](#known-limitations)
- [Interview Cheat Sheet](#interview-cheat-sheet)

---

## Architecture Overview

```
[order_sender / cli]
        │
        │  UDP packet (32 bytes, raw binary)  ← port 9001
        │
        ▼
[UDP Socket — non-blocking, busy-poll]
        │
        │  recvfrom() — spins, never blocks
        │  stamp recv_tsc immediately after return
        │
        ▼
[SPSC Ring Buffer — 4096 slots, lock-free]
        │
        │  push: release store on tail_
        │  pop:  acquire load  on tail_
        │
        ▼
[Matching Engine Thread]
        │
        ├── compute latency (rdtscp − recv_tsc)
        ├── LIMIT  → OrderBook::addAndMatch()
        └── CANCEL → OrderBook::cancelOrder()
                │
                ▼
        [Limit Order Book]
        ├── bids: std::map (descending price)
        └── asks: std::map (ascending price)
                │
                ▼
        Trade fires → stdout + LatencyStats::record()
                │
                ▼
        [LatencyStats] → percentile report on SIGINT
```

Two OS threads. One UDP socket. Zero mutexes. Zero `malloc` on the hot path.

---

## System Design

### `order.h` — The Atom of the System

Every component in the project exists to move, store, or process a single `Order`:

```cpp
struct alignas(8) Order {
    uint64_t  order_id;   // unique ID assigned by the sender
    uint64_t  price;      // fixed-point ×10000  ($180.25 → 1802500)
    uint64_t  recv_tsc;   // TSC counter at network receipt
    uint32_t  quantity;   // number of shares
    Side      side;       // BUY = 0 | SELL = 1
    OrderType type;       // LIMIT = 0 | CANCEL = 1
    uint8_t   _pad[2];    // explicit padding → 32 bytes total
};
static_assert(sizeof(Order) == 32);
```

**Design decisions:**
- `alignas(8)` — 8-byte alignment guarantees atomic loads/stores on x86
- **Fixed-point price** — `float`/`double` cannot exactly represent most decimal fractions; rounding errors compound across millions of trades. Fixed-point at 4 decimal places is exact, fast to compare, and trivial to serialize
- `recv_tsc` **embedded in the struct** — it rides the SPSC queue for free; no extra IPC channel needed between the network and engine threads
- **32 bytes** — fits in half a cache line; two orders share one cache line fetch

---

### `spsc_queue.h` — The Bridge Between Threads

A templated, compile-time-sized **Single-Producer Single-Consumer** ring buffer. Allows the network thread and the engine thread to communicate with zero locks and zero syscalls.

**False sharing prevention — cache line isolation:**

```
WITHOUT padding                     WITH padding (our design)
┌────────────────────────┐          ┌──────────────────────────────────────────┐
│ head_ │ tail_ │ ...    │          │ head_ │ <56 bytes padding>               │  ← cache line 1 (consumer)
└────────────────────────┘          ├──────────────────────────────────────────┤
  ↑ both on the same cache line     │ tail_ │ <56 bytes padding>               │  ← cache line 2 (producer)
                                    └──────────────────────────────────────────┘
```

Without `alignas(64)` padding, every `tail_` write by the producer invalidates the consumer's cache line — even though the consumer never touched `tail_`. This **false sharing** can halve throughput on a multi-core system.

**Memory ordering — the correctness guarantee:**

```cpp
// Producer (network thread):
slots_[tail] = item;
tail_.store(next, std::memory_order_release);   // "I am done writing the slot"

// Consumer (engine thread):
if (head == tail_.load(std::memory_order_acquire))  // "I see your store"
    return std::nullopt;
T item = slots_[head];   // guaranteed safe to read
```

`release` + `acquire` form a **happens-before** edge. Without this, the CPU (or compiler) could reorder the slot write *after* the tail update, causing the consumer to read uninitialized memory. This is a real, non-hypothetical bug on ARM/PowerPC; x86's TSO model happens to paper over it, but the code would still be undefined behavior.

**Capacity:** 4096 slots (power-of-two enforced by `static_assert`) → ~96 KB for `Order` payloads.

---

### `order_book.h` / `order_book.cpp` — The Core Business Logic

A classic **price-time priority limit order book** with two sorted price-level maps and an O(1) cancel index.

**Internal structure:**

```cpp
// Bids: highest price has priority → descending order
std::map<uint64_t, PriceLevel, std::greater<uint64_t>> bids_;

// Asks: lowest price has priority → ascending order (default)
std::map<uint64_t, PriceLevel>                          asks_;

// O(1) cancel: order_id → {side, price_level}
std::unordered_map<uint64_t, OrderLocation>             order_index_;
```

`PriceLevel` is a `std::deque<Order>` — `push_back` (new order) and `pop_front` (fill) are both O(1), preserving FIFO time priority within a price level.

**Matching loop:**

```
while best_bid.price >= best_ask.price:
    fill_qty = min(bid.quantity, ask.quantity)
    emit Trade(buy_id, sell_id, price=ask.price, qty=fill_qty)
    deduct fill_qty from both sides
    remove fully-filled orders
    remove now-empty price levels
```

Trades always execute at the **ask price** (resting order convention). The aggressor (incoming order) accepts the resting order's price.

**Cancel — why `order_index_` exists:**
Without the index, cancelling order #42 would require scanning every price level — O(N). With it, cancel is O(1): look up `{side, price}`, jump directly to that level's deque, erase. The cost is one extra map entry per live order.

---

### `memory_pool.h` — Zero Runtime Allocation

A fixed-capacity, slab-allocated **intrusive freelist** pool. `alloc()` costs ~2 ns and touches no kernel code.

**The intrusive trick — one slot, two lives:**

```
Slot is FREE:      [ next_ptr (8 bytes) | ... unused bytes ... ]
Slot is ALLOCATED: [ T object data (sizeof(T) bytes)           ]
```

The same 32 bytes serve dual purpose. When free, the first 8 bytes store a pointer to the next free node. When allocated, those bytes are overwritten by placement-new.

**LIFO freelist — cache locality by design:**
The most recently freed slot is hottest in L1/L2 cache. LIFO reuse means the next `alloc()` almost always hits cached memory, saving ~100 ns vs. a cold-cache heap allocation.

**Hot path:**
```cpp
T* alloc() {
    Slot* slot = free_head_;       // 1 load
    free_head_ = slot->next;       // 1 store
    return reinterpret_cast<T*>(slot);
}
```
Two pointer moves. No syscall. No lock. No `malloc`.

---

### `latency.h` — Latency Measurement via TSC Hardware Timer

**Why TSC instead of `clock_gettime`:**

| Method | Mechanism | Overhead |
|---|---|---|
| `clock_gettime` | user → kernel → read HPET/APIC → user | ~50–200 ns (syscall) |
| `rdtsc` | single x86 instruction, reads hardware register | ~5 ns, stays in user space |

**Calibration (done once at startup):**

```
1. Record wall time t0 + TSC counter tsc0
2. Spin for 100 ms
3. Record wall time t1 + TSC counter tsc1
4. ns_per_tick = (t1 - t0) ns / (tsc1 - tsc0) ticks
```

Every subsequent measurement multiplies TSC delta by this constant — no more syscalls on the hot path.

**Why `rdtscp` at the end but `rdtsc` at the start:**
`rdtscp` **serializes** — it waits for all prior instructions to retire before reading the counter. At the end of a measurement this prevents the CPU from counting time before the measured work actually finished. At the start, `rdtsc` is fine (and faster) — we want the timer running as early as possible.

**Reading the percentile report (printed on Ctrl-C):**

| Metric | Meaning |
|---|---|
| Min | Best case — warm caches, no interrupts |
| p50 | Typical case — what half your orders experience |
| p99 | Tail latency — 1 in 100 orders |
| p99.9 | Extreme tail — OS interrupts, cache misses, page faults |
| Max | Worst-case spike — often a hypervisor steal or GC pause |

The gap between p50 and p99.9 is **jitter** — the primary enemy of HFT systems.

---

### `udp_receiver.h` / `udp_receiver.cpp` — The Network Layer

Listens on **UDP port 9001**. The socket is set non-blocking; the thread busy-polls in a tight loop.

**Busy-poll vs. blocking:**

```cpp
// Blocking (bad for latency):
recvfrom(...);  // parks thread in kernel until packet arrives
// kernel wakeup → context switch → 5–50 µs added latency

// Busy-poll (our design):
while (running_) {
    int n = recvfrom(...);                        // returns immediately
    if (n < 0 && errno == EAGAIN) continue;       // spin
    // packet arrived → process immediately: ~1–2 µs
}
```

Cost: one CPU core runs at 100 % permanently. Acceptable in HFT environments where cores are dedicated.

**TSC stamp placement — every nanosecond counts:**

```cpp
ssize_t n = recvfrom(sockfd_, buf, sizeof(Order), 0, ...);
uint64_t tsc_now = rdtsc();   // ← stamp HERE, as close to recvfrom as possible
order.recv_tsc = tsc_now;
cb(order);
```

Every nanosecond between `recvfrom` return and the TSC stamp is dead time charged to measured latency.

---

### `tuning.h` — Linux Performance Tuning

Header-only collection of OS-level latency knobs applied at startup.

| Technique | What it does | Latency impact |
|---|---|---|
| `SCHED_FIFO` | Real-time scheduler — prevents OS preemption of your thread | Eliminates 5–50 µs spikes |
| `mlockall` | Pins all memory pages in RAM — no page faults | Eliminates ~1 ms page-fault spikes |
| `isolcpus` (kernel boot param) | Reserves cores — kernel schedules no other processes there | Eliminates ~10 µs scheduler interference |
| CPU pinning (`taskset`) | Thread locked to specific core — no migration | Eliminates cache invalidation from core-hops |
| `__builtin_prefetch` | Loads next cache line before it is needed | Hides ~100 ns memory latency |
| `LIKELY` / `UNLIKELY` | Branch-predictor hints via `__builtin_expect` | ~1–5 ns per mispredicted branch |
| `FORCE_INLINE` | Forces inlining of hot-path functions | Eliminates function-call overhead |

The engine thread also emits **`__builtin_ia32_pause()`** in its spin loop. This x86 `PAUSE` instruction signals a spin-wait to the CPU, reduces power consumption, prevents memory-order violation pipeline flushes, and — on hyperthreaded cores — yields execution resources to the sibling thread. Without it, the spin loop can starve the producer thread sharing the same physical core.

---

## Project Structure

```
flashbook/
├── include/
│   ├── order.h           # Core Order struct + Side/OrderType enums
│   ├── spsc_queue.h      # Lock-free SPSC ring buffer (templated)
│   ├── order_book.h      # Limit order book declaration
│   ├── memory_pool.h     # Intrusive freelist memory pool (templated)
│   ├── latency.h         # TSC clock calibration + LatencyStats
│   ├── udp_receiver.h    # UDPReceiver declaration
│   ├── engine.h          # MatchingEngine declaration
│   ├── tuning.h          # OS latency-tuning helpers (header-only)
│   └── logger.h          # Lightweight logger
├── src/
│   ├── main.cpp          # Entry point — wires all components together
│   ├── udp_receiver.cpp  # Non-blocking UDP receive + TSC stamp
│   ├── engine.cpp        # Engine thread: dequeue → match → record latency
│   └── order_book.cpp    # addAndMatch(), cancelOrder() implementations
├── tools/
│   ├── cli.cpp           # Interactive terminal order-entry UI
│   ├── order_sender.cpp  # Single UDP order injection (scripted testing)
│   ├── test_book.cpp     # Isolated order book harness (no network)
│   ├── test_pool.cpp     # Memory pool correctness + speed benchmark
│   ├── bench_latency.cpp # Pure engine latency benchmark (no UDP jitter)
│   └── sample_replay.txt # Example order replay script for cli
└── CMakeLists.txt
```

---

## Building

**Requirements:** GCC or Clang with C++17 support, CMake ≥ 3.16, pthreads.

```bash
# Clone and enter the project
git clone https://github.com/SujalBShirodkar/FlashBook.git
cd flashbook

# Configure + build (all targets)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Compiler flags applied: `-O2 -Wall -Wextra -march=native -g`

> **Tip:** Swap `-O2` for `-O3` when running benchmarks for maximum performance.

---

## Running

**Terminal 1 — Start the matching engine:**

```bash
./build/flashbook
```

The engine binds UDP port 9001, calibrates the TSC clock, applies OS tuning knobs, then spins waiting for orders. Press **Ctrl-C** to print the latency percentile report and exit.

**Terminal 2 — Send orders interactively:**

```bash
./build/cli
```

Or inject a single order via script:

```bash
./build/order_sender
```

**Isolated testing (no network required):**

```bash
./build/test_book        # step through order matching manually
./build/bench_latency    # measure pure engine latency without UDP jitter
./build/test_pool        # verify memory pool correctness and measure alloc speed
```

**For lowest latency on bare metal:**

```bash
sudo cpupower frequency-set -g performance
sudo sh -c 'echo 0 > /proc/sys/kernel/nmi_watchdog'
taskset -c 0 ./build/flashbook     # pin engine to core 0
```

---

## Tools Reference

| Binary | Purpose |
|---|---|
| `./build/flashbook` | Main matching engine — always running in Terminal 1 |
| `./build/cli` | Interactive order entry — human-friendly terminal UI |
| `./build/order_sender` | Single UDP order injection — quick scripted testing |
| `./build/test_book` | Isolated order book testing — debug matching logic, no network |
| `./build/test_pool` | Memory pool correctness + speed benchmark |
| `./build/bench_latency` | Pure engine latency (eliminates UDP jitter from measurements) |

---

## Benchmark Results

Measured on **WSL2** (Ubuntu on Windows). Bare metal numbers are significantly better — WSL's hypervisor introduces spurious latency on spinning threads.

| Measurement | WSL2 Result | Bare Metal Expectation |
|---|---|---|
| Memory pool `alloc()` | **2.12 ns** | 1–3 ns |
| `malloc` (for comparison) | 8.48 ns | 20–100 ns (worse under load) |
| TSC read overhead (`rdtsc`) | 9.56 ns | 3–5 ns |
| Engine **min** latency | **360 ns** | 100–300 ns |
| Engine **p50** latency | ~162 ms | 200–500 ns |

> The 360 ns minimum reflects true engine speed. The inflated p50/mean is entirely WSL2's hypervisor stealing CPU time from spinning threads — not a reflection of code quality. On a dedicated bare-metal Linux host with `isolcpus` and `SCHED_FIFO`, p50 would be in the hundreds-of-nanoseconds range.

---

## License

[MIT](LICENSE)

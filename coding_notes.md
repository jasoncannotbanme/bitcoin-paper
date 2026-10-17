# coding_notes.md

gcc linker failure on C timer test:
gcc -std=c17 -O2 latency_examples.c -o latency_examples_c.exe
throws:
undefined reference to `QueryPerformanceCounter'
undefined reference to `QueryPerformanceFrequency'
-> mingw didnt link kernel32 automatically with -std=c17. added -lkernel32 and #include <windows.h>.

WHY IS THE TIMER RETURNING 0.0 NS???
output kept printing:
0.00 ns
0.00 ns
100.00 ns
0.00 ns
checked QueryPerformanceFrequency -> 10,000,000 counts/sec = 100ns per tick.
my parse function takes ~11ns.
if clock doesn't tick during the 11ns call, delta = 0!
if clock ticks once, delta = 100ns!
fix: time batches of 50 or 100 calls in a loop:
t0 = get_time();
for (int i = 0; i < 100; i++) sink += parse_price(prices[i]);
t1 = get_time();
latency = (t1 - t0) / 100.0;
now getting 11.4ns median.

compiler deleted my whole benchmark loop wtf:
g++ -std=c++20 -O2 latency_examples.cpp
order book ladder update printed 0.02 ns.
inspected with -S: GCC saw ladder was never read after the timing loop, so dead code elimination deleted all 10,000 iterations!
fix: added volatile dummy sink:
volatile uint64_t dummy = ladder.best_bid_price() ^ ladder.best_ask_qty();
ladder update now prints realistic 8.2 ns.

spsc ring buffer was 2x SLOWER than mutex (false sharing nightmare):
std::mutex queue: 75.5 ns
my lock-free queue: 138.4 ns!
why is lock-free queue slower than mutex?
checked struct:
struct SPSC {
    std::atomic<size_t> head_{0}; // 8 bytes
    std::atomic<size_t> tail_{0}; // 8 bytes
};
head and tail sat in the same 64-byte L1 cache line!
core 2 (producer) writes head -> invalidates core 4 L1 cache.
core 4 (consumer) writes tail -> invalidates core 2 L1 cache.
cores were thrashing the memory bus on every single message (cache line bouncing).
fix: added 64-byte padding:
alignas(64) std::atomic<size_t> head_{0};
alignas(64) std::atomic<size_t> tail_{0};
re-ran -> latency dropped to 28.7 ns (2.6x faster than mutex).

segfault on ladder replay:
running --scaling up to 262144 levels:
Crash: Access violation reading location 0x00007FF7B40A1000
mid price in synthetic walk drifted by >2000 ticks.
idx = (price - base) / tick overflowed the 4096 ladder array!
fix: added boundary check:
if (price < base || price >= base + 4096 * tick) return;

memory pool test failed:
custom fixed-block pool vs malloc:
fresh malloc: 156 ns
reused buffer: 104 ns
custom pool: 108 ns
custom pool didn't beat buffer reuse and barely beat malloc.
why? windows ucrt uses Low Fragmentation Heap (LFH) which caches blocks <256 bytes in thread-local bins anyway. my custom pool added singly-linked free-list pointer manipulation overhead.
decided not to hide this - keep the honest numbers in Table 7.

pipeline & burst terminal log from advanced_examples:
advanced_examples.cpp | compiler: 16.1.0 | hardware threads: 16
steady_clock smallest observed step: 100 ns

                           p50       p95       p99      mean
  malloc / free fresh:    156.0     212.0     285.0     164.2
  reused static buffer:   104.0     135.0     170.0     108.5
  fixed-block pool:       108.0     140.0     178.0     112.1

  Pipeline benchmark (10-level message):
    from_chars + std::map:           2860.0 ns (p50)
    fast_parse + ladder:              496.0 ns (p50)
    Speedup: 5.8x

  Burst processing throughput:
    Burst size: 50 messages  -> 492.1 ns/msg | 2.03 M msg/sec
    Burst size: 100 messages -> 488.4 ns/msg | 2.05 M msg/sec
    Burst size: 500 messages -> 479.2 ns/msg | 2.09 M msg/sec
    Burst size: 1000 messages-> 481.5 ns/msg | 2.08 M msg/sec

burst queue math:
500 messages:
baseline: 500 * 2.86 us = 1.43 ms backlog!
optimised: 500 * 0.48 us = 0.24 ms (240 us)!

matplotlib notes:
claude helped write make_new_figures.py.
tight_layout clipped the log tick labels on figure 1 and 2, had to add ticker.FuncFormatter.
used serif font to match springer template.
exported 6 figures at 300 dpi.

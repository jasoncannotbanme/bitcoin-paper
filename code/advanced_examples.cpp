// advanced_examples.cpp
//
// Additional C++20 examples for the paper "Reducing Latency in
// Proprietary Cryptocurrency High-Frequency Trading".
//
//   Example A: Fixed-block memory pool vs. malloc/free
//   Example B: Full pipeline benchmark (parse -> book -> signal)
//   Example C: Batch message processing throughput
//
// Build:  g++ -std=c++20 -O2 -pthread advanced_examples.cpp
//             -o advanced_examples
// Run:    ./advanced_examples
//
// All data is synthetic. The program does not open any network
// connection, needs no API credentials, and cannot place orders.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <string_view>
#include <vector>

namespace {

// ------------------------------------------------------------------
// Common types (same as latency_examples.cpp)
// ------------------------------------------------------------------

struct Update {
    std::int64_t price;
    std::int64_t qty;
    bool is_bid;
};

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed) {}
    std::uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
};

using Clock = std::chrono::steady_clock;

struct Stats {
    double p50, p95, p99, mean;
};

Stats summarize(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    auto pct = [&](double p) {
        std::size_t rank = static_cast<std::size_t>(
            std::ceil(p * static_cast<double>(v.size())));
        return v[rank == 0 ? 0 : rank - 1];
    };
    double sum = 0.0;
    for (double d : v) sum += d;
    return {pct(0.50), pct(0.95), pct(0.99),
            sum / static_cast<double>(v.size())};
}

template <class Op>
Stats bench(std::size_t warmup, std::size_t batches, std::size_t batch,
            Op&& op) {
    std::vector<double> ns;
    ns.reserve(batches);
    std::size_t i = 0;
    for (std::size_t b = 0; b < warmup + batches; ++b) {
        const auto t0 = Clock::now();
        for (std::size_t k = 0; k < batch; ++k) op(i++);
        const auto t1 = Clock::now();
        if (b >= warmup) {
            ns.push_back(
                std::chrono::duration<double, std::nano>(t1 - t0).count() /
                static_cast<double>(batch));
        }
    }
    return summarize(ns);
}

void report(const char* name, const Stats& s) {
    std::printf("  %-40s %9.1f %9.1f %9.1f %9.1f\n", name, s.p50, s.p95,
                s.p99, s.mean);
}

// ------------------------------------------------------------------
// Order book (reproduced for self-contained compilation)
// ------------------------------------------------------------------

constexpr std::int64_t kMid = 6725000;
constexpr std::int64_t kLevels = 4096;
constexpr std::int64_t kBase = kMid - kLevels / 2;

class LadderBook {
public:
    LadderBook(std::int64_t base, std::int64_t levels)
        : base_(base), n_(levels),
          bid_(static_cast<std::size_t>(levels), 0),
          ask_(static_cast<std::size_t>(levels), 0),
          best_bid_(-1), best_ask_(levels) {}

    bool apply(const Update& u) {
        const std::int64_t i = u.price - base_;
        if (i < 0 || i >= n_) return false;
        if (u.is_bid) {
            bid_[i] = u.qty;
            if (u.qty > 0) {
                if (i > best_bid_) best_bid_ = i;
            } else if (i == best_bid_) {
                while (best_bid_ >= 0 && bid_[best_bid_] == 0)
                    --best_bid_;
            }
        } else {
            ask_[i] = u.qty;
            if (u.qty > 0) {
                if (i < best_ask_) best_ask_ = i;
            } else if (i == best_ask_) {
                while (best_ask_ < n_ && ask_[best_ask_] == 0)
                    ++best_ask_;
            }
        }
        return true;
    }
    std::int64_t best_bid() const {
        return best_bid_ >= 0 ? base_ + best_bid_ : 0;
    }
    std::int64_t best_ask() const {
        return best_ask_ < n_ ? base_ + best_ask_ : 0;
    }

private:
    std::int64_t base_, n_;
    std::vector<std::int64_t> bid_, ask_;
    std::int64_t best_bid_, best_ask_;
};

// ------------------------------------------------------------------
// Parser (reproduced for self-contained compilation)
// ------------------------------------------------------------------

bool skip(const char*& p, const char* end, std::string_view lit) {
    if (static_cast<std::size_t>(end - p) < lit.size() ||
        std::string_view(p, lit.size()) != lit)
        return false;
    p += lit.size();
    return true;
}

bool parse_fixed(const char*& p, const char* end, int decimals,
                 std::int64_t& out) {
    const char* const start = p;
    std::int64_t v = 0;
    int frac = -1;
    for (; p < end; ++p) {
        const char c = *p;
        if (c >= '0' && c <= '9') {
            if (frac == decimals) continue;
            if (frac >= 0) ++frac;
            v = v * 10 + (c - '0');
        } else if (c == '.' && frac < 0) {
            frac = 0;
        } else break;
    }
    if (p == start) return false;
    for (int f = frac < 0 ? 0 : frac; f < decimals; ++f) v *= 10;
    out = v;
    return true;
}

int parse_fast(std::string_view msg, Update* out, int cap) {
    const char* p = msg.data();
    const char* const end = p + msg.size();
    int n = 0;
    for (const char side : {'b', 'a'}) {
        const char key[5] = {'"', side, '"', ':', '['};
        p = std::search(p, end, key, key + 5);
        if (p == end) return -1;
        p += 5;
        while (p < end && *p == '[') {
            if (n == cap) return -1;
            if (!skip(p, end, "[\"") ||
                !parse_fixed(p, end, 2, out[n].price) ||
                !skip(p, end, "\",\"") ||
                !parse_fixed(p, end, 8, out[n].qty) ||
                !skip(p, end, "\"]"))
                return -1;
            out[n++].is_bid = (side == 'b');
            if (p < end && *p == ',') ++p;
        }
    }
    return n;
}

// ------------------------------------------------------------------
// SPSC ring buffer (reproduced)
// ------------------------------------------------------------------

template <class T>
class SpscRing {
public:
    explicit SpscRing(std::size_t capacity_pow2)
        : mask_(capacity_pow2 - 1), buf_(capacity_pow2) {}

    bool try_push(const T& v) {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t - head_.load(std::memory_order_acquire) > mask_)
            return false;
        buf_[t & mask_] = v;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
    bool try_pop(T& v) {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        if (h == tail_.load(std::memory_order_acquire))
            return false;
        v = buf_[h & mask_];
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

private:
    const std::size_t mask_;
    std::vector<T> buf_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

// ------------------------------------------------------------------
// Example A: Fixed-block memory pool
// ------------------------------------------------------------------

class FixedPool {
public:
    FixedPool(std::size_t block_size, std::size_t capacity)
        : block_size_((block_size + 63) & ~std::size_t(63)),
          capacity_(capacity),
          top_(capacity) {
        base_ = static_cast<char*>(std::malloc(block_size_ * capacity));
        stack_ = static_cast<char**>(std::malloc(capacity * sizeof(char*)));
        if (!base_ || !stack_) {
            std::fprintf(stderr, "pool allocation failed\n");
            std::exit(EXIT_FAILURE);
        }
        for (std::size_t i = 0; i < capacity; ++i)
            stack_[i] = base_ + i * block_size_;
    }
    ~FixedPool() { std::free(stack_); std::free(base_); }

    void* alloc() { return top_ > 0 ? stack_[--top_] : nullptr; }
    void  free(void* p) { stack_[top_++] = static_cast<char*>(p); }

private:
    char*   base_;
    char**  stack_;
    std::size_t block_size_;
    std::size_t capacity_;
    std::size_t top_;
};

void run_memory_pool() {
    constexpr std::size_t block = 384;  // slightly larger than a message
    constexpr std::size_t warmup = 2000, batches = 20000, batch = 100;

    // Baseline: malloc + use + free per call
    volatile char sink = 0;
    const Stats s_malloc = bench(warmup, batches, batch, [&](std::size_t) {
        char* p = static_cast<char*>(std::malloc(block));
        if (!p) std::exit(EXIT_FAILURE);
        std::memset(p, 0x42, block);
        sink = p[0];
        std::free(p);
    });

    // Optimized: fixed pool alloc + use + free
    FixedPool pool(block, (warmup + batches) * batch + 1024);
    const Stats s_pool = bench(warmup, batches, batch, [&](std::size_t) {
        char* p = static_cast<char*>(pool.alloc());
        if (!p) std::exit(EXIT_FAILURE);
        std::memset(p, 0x42, block);
        sink = p[0];
        pool.free(p);
    });

    std::printf("Example A: memory allocation for %zu-byte blocks "
                "(ns per alloc+use+free)\n", block);
    report("malloc + memset + free (baseline)", s_malloc);
    report("fixed pool + memset + free", s_pool);
    std::printf("\n");
}

// ------------------------------------------------------------------
// Example B: Full pipeline (parse -> book update -> signal)
// ------------------------------------------------------------------

struct Signal {
    std::int64_t best_bid;
    std::int64_t best_ask;
    std::int64_t recv_time;
};

Update make_update(Rng& r) {
    const std::uint64_t a = r.next();
    const std::int64_t off =
        1 + static_cast<std::int64_t>(((a & 1023) * ((a >> 10) & 1023)) >> 10);
    const bool is_bid = ((a >> 20) & 1) != 0;
    const bool del = ((a >> 21) % 10) < 3;
    const std::int64_t qty =
        del ? 0 : 1 + static_cast<std::int64_t>((a >> 25) % 100000000);
    return {is_bid ? kMid - off : kMid + off, qty, is_bid};
}

std::vector<std::string> make_messages(std::size_t count, int per_side) {
    Rng r(7);
    std::vector<std::string> msgs;
    msgs.reserve(count);
    char tmp[64];
    for (std::size_t m = 0; m < count; ++m) {
        std::string s = "{\"e\":\"depthUpdate\",\"E\":";
        s += std::to_string(1700000000000ULL + m);
        s += ",\"s\":\"BTCUSDT\",\"U\":" + std::to_string(10 * m + 1);
        s += ",\"u\":" + std::to_string(10 * m + 10);
        for (int side = 0; side < 2; ++side) {
            s += side == 0 ? ",\"b\":[" : ",\"a\":[";
            for (int k = 0; k < per_side; ++k) {
                Update u = make_update(r);
                while (u.is_bid != (side == 0)) u = make_update(r);
                std::snprintf(tmp, sizeof tmp,
                              "%s[\"%lld.%02lld\",\"%lld.%08lld\"]",
                              k ? "," : "",
                              static_cast<long long>(u.price / 100),
                              static_cast<long long>(u.price % 100),
                              static_cast<long long>(u.qty / 100000000),
                              static_cast<long long>(u.qty % 100000000));
                s += tmp;
            }
            s += "]";
        }
        s += "}";
        msgs.push_back(std::move(s));
    }
    return msgs;
}

void run_full_pipeline() {
    const int per_side = 5;
    const std::size_t distinct = 4096;
    const auto msgs = make_messages(distinct, per_side);

    const std::size_t warmup = 1000, batches = 10000, batch = 50;
    std::int64_t checksum = 0;

    // Full optimized pipeline: parse -> ladder book -> signal struct
    LadderBook ladder(kBase, kLevels);
    const Stats s_full = bench(warmup, batches, batch, [&](std::size_t i) {
        const auto& msg = msgs[i % distinct];
        Update levels[64];
        int n = parse_fast(msg, levels, 64);
        if (n < 0) std::exit(EXIT_FAILURE);
        for (int k = 0; k < n; ++k) ladder.apply(levels[k]);
        Signal sig;
        sig.best_bid = ladder.best_bid();
        sig.best_ask = ladder.best_ask();
        sig.recv_time = static_cast<std::int64_t>(i);
        checksum += sig.best_bid + sig.best_ask;
    });

    std::printf("Example B: full pipeline parse->book->signal "
                "(ns per message, %d levels)\n", 2 * per_side);
    report("parse_fast + LadderBook + Signal", s_full);
    std::printf("  checksum: %lld\n\n", static_cast<long long>(checksum));
}

// ------------------------------------------------------------------
// Example C: Batch message processing (burst simulation)
// ------------------------------------------------------------------

void run_burst_simulation() {
    const int per_side = 5;
    const std::size_t burst_sizes[] = {10, 50, 100, 500, 1000};
    const auto msgs = make_messages(1000, per_side);

    LadderBook ladder(kBase, kLevels);
    Update levels[64];

    std::printf("Example C: burst processing (total time for N messages)\n");
    std::printf("  %-15s %12s %15s %15s\n",
                "Burst size", "Total (us)", "Per-msg (ns)", "Throughput (M/s)");

    for (std::size_t burst : burst_sizes) {
        // Warm up
        for (int w = 0; w < 5000; ++w) {
            int n = parse_fast(msgs[w % msgs.size()], levels, 64);
            for (int k = 0; k < n; ++k) ladder.apply(levels[k]);
        }

        // Measure: process `burst` messages back-to-back
        const int trials = 200;
        std::vector<double> times;
        times.reserve(trials);

        for (int t = 0; t < trials; ++t) {
            const auto t0 = Clock::now();
            for (std::size_t m = 0; m < burst; ++m) {
                int n = parse_fast(msgs[m % msgs.size()], levels, 64);
                for (int k = 0; k < n; ++k) ladder.apply(levels[k]);
            }
            const auto t1 = Clock::now();
            times.push_back(
                std::chrono::duration<double, std::nano>(t1 - t0).count());
        }

        std::sort(times.begin(), times.end());
        double median_total_ns = times[trials / 2];
        double per_msg = median_total_ns / static_cast<double>(burst);
        double throughput = 1e9 / per_msg / 1e6;

        std::printf("  %-15zu %12.1f %15.1f %15.2f\n",
                    burst,
                    median_total_ns / 1000.0,
                    per_msg,
                    throughput);
    }
    std::printf("\n");
}

}  // namespace

int main() {
    std::printf("advanced_examples.cpp | compiler: %s | "
                "hardware threads: %u\n",
                __VERSION__, std::thread::hardware_concurrency());

    double best = 1e18;
    for (int i = 0; i < 200000; ++i) {
        const auto a = Clock::now();
        auto b = Clock::now();
        while (b == a) b = Clock::now();
        best = std::min(
            best, std::chrono::duration<double, std::nano>(b - a).count());
    }
    std::printf("steady_clock smallest observed step: %.0f ns\n\n", best);

    std::printf("  %-40s %9s %9s %9s %9s\n", "", "p50", "p95", "p99", "mean");

    run_memory_pool();
    run_full_pipeline();
    run_burst_simulation();

    return EXIT_SUCCESS;
}

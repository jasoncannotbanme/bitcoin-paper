// latency_examples.cpp
//
// Illustrative C++20 examples for the paper "Reducing Latency in
// High-Frequency Bitcoin Trading: A Literature Review".
//
//   Example 1: order-book updates, std::map versus a flat price ladder
//   Example 2: market-data parsing, allocating versus zero-copy
//              (uses <charconv> floating-point from_chars: GCC 11+,
//              MSVC 2019 16.4+, or a recent libc++)
//   Example 3: passing messages between threads, mutex queue versus a
//              single-producer single-consumer ring buffer
//
// Build:  g++ -std=c++20 -O2 -pthread latency_examples.cpp
//             -o latency_examples_cpp
// Run:    ./latency_examples_cpp            (Examples 1 to 3)
//         ./latency_examples_cpp --scaling  (Example 1 at several book
//                                            depths, printed as CSV)
//
// All market data is synthetic. The program does not open any network
// connection, needs no API credentials, and cannot place orders.

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

// ------------------------------------------------------------------
// Common types and helpers
// ------------------------------------------------------------------

// One price-level update. Prices are integers in ticks of 0.01 USD and
// quantities are integers in units of 1e-8 BTC, so no floating point is
// needed after parsing. A quantity of zero deletes the level.
struct Update {
    std::int64_t price;
    std::int64_t qty;
    bool is_bid;
};

// Small deterministic generator (xorshift64) so that every run and every
// standard library produces the same synthetic workload.
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

using Clock = std::chrono::steady_clock;  // monotonic

struct Stats {
    double p50, p95, p99, mean;
};

// Nearest-rank percentiles of the samples in v (v is sorted in place).
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

// Calls op(i) for i = 0, 1, 2, ... and times it in batches. One sample is
// the mean time per call within one batch. Batching is needed because a
// single call can be shorter than the resolution of the clock. The first
// `warmup` batches are executed but not recorded.
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
    std::printf("  %-34s %9.1f %9.1f %9.1f %9.1f\n", name, s.p50, s.p95,
                s.p99, s.mean);
}

// Smallest non-zero step of the clock that we can observe, in ns.
double clock_step_ns() {
    double best = 1e18;
    for (int i = 0; i < 200000; ++i) {
        const auto a = Clock::now();
        auto b = Clock::now();
        while (b == a) b = Clock::now();
        best = std::min(
            best, std::chrono::duration<double, std::nano>(b - a).count());
    }
    return best;
}

// ------------------------------------------------------------------
// Example 1: order book
// ------------------------------------------------------------------

constexpr std::int64_t kMid = 6725000;   // 67,250.00 USD in ticks
constexpr std::int64_t kLevels = 4096;   // size of the ladder window
constexpr std::int64_t kBase = kMid - kLevels / 2;

// Baseline: one balanced tree per side. Every operation is O(log n) and
// every new level is a separate heap node.
class MapBook {
public:
    void apply(const Update& u) {
        if (u.is_bid) {
            if (u.qty == 0) bids_.erase(u.price);
            else bids_[u.price] = u.qty;
        } else {
            if (u.qty == 0) asks_.erase(u.price);
            else asks_[u.price] = u.qty;
        }
    }
    std::int64_t best_bid() const {
        return bids_.empty() ? 0 : bids_.begin()->first;
    }
    std::int64_t best_ask() const {
        return asks_.empty() ? 0 : asks_.begin()->first;
    }
    std::size_t levels() const { return bids_.size() + asks_.size(); }

private:
    std::map<std::int64_t, std::int64_t, std::greater<>> bids_;
    std::map<std::int64_t, std::int64_t> asks_;
};

// --- snippet: ladder ---
// Optimized: quantities live in two contiguous arrays indexed by
// (price - base). An update is one array write, O(1). The best
// price is cached and only rescanned when the best level empties.
class LadderBook {
public:
    LadderBook(std::int64_t base, std::int64_t levels)
        : base_(base), n_(levels),
          bid_(static_cast<std::size_t>(levels), 0),
          ask_(static_cast<std::size_t>(levels), 0),
          best_bid_(-1), best_ask_(levels) {}

    // Returns false if the price is outside the window. The
    // caller must then re-center the ladder (not shown).
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
// --- end snippet ---
    std::size_t bytes() const {
        return (bid_.size() + ask_.size()) * sizeof(std::int64_t);
    }

private:
    std::int64_t base_, n_;
    std::vector<std::int64_t> bid_, ask_;
    std::int64_t best_bid_, best_ask_;
};

// Synthetic level updates around a fixed mid price. Bids are below the
// mid and asks above it, so the book never crosses. Offsets are skewed
// toward the top of the book and about 30% of updates delete a level.
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

std::vector<Update> make_updates(std::size_t n) {
    Rng r(42);
    std::vector<Update> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; ++i) v.push_back(make_update(r));
    return v;
}

void run_order_book() {
    const std::size_t warmup = 2000, batches = 20000, batch = 100;
    const auto updates = make_updates((warmup + batches) * batch);

    MapBook map_book;
    std::int64_t sum_map = 0;
    const Stats s_map = bench(warmup, batches, batch, [&](std::size_t i) {
        map_book.apply(updates[i]);
        sum_map += map_book.best_bid() + map_book.best_ask();
    });

    LadderBook ladder(kBase, kLevels);
    std::int64_t sum_ladder = 0;
    bool in_window = true;
    const Stats s_ladder = bench(warmup, batches, batch, [&](std::size_t i) {
        in_window &= ladder.apply(updates[i]);
        sum_ladder += ladder.best_bid() + ladder.best_ask();
    });

    std::printf("Example 1: order-book update + best bid/ask (ns per update)\n");
    report("std::map book (baseline)", s_map);
    report("flat price ladder", s_ladder);
    std::printf("  results identical: %s, ladder memory: %zu bytes\n\n",
                (in_window && sum_map == sum_ladder) ? "yes" : "NO",
                ladder.bytes());
    if (!in_window || sum_map != sum_ladder) std::exit(EXIT_FAILURE);
}

// Example 1 repeated for books of different depth. Offsets are uniform in
// [1, width], so about 70% of the 2 * width possible levels are occupied.
// The ladder is sized to fit the width, so its memory grows with it.
void run_scaling() {
    const std::size_t warmup = 2000, batches = 10000, batch = 100;
    std::printf("width,active_levels,ladder_bytes,"
                "map_p50,map_p95,map_p99,ladder_p50,ladder_p95,ladder_p99\n");
    for (const std::int64_t width :
         {16, 64, 256, 1024, 4096, 16384, 65536, 262144}) {
        Rng r(42);
        std::vector<Update> updates;
        updates.reserve((warmup + batches) * batch);
        for (std::size_t i = 0; i < (warmup + batches) * batch; ++i) {
            const std::uint64_t a = r.next();
            const std::int64_t off =
                1 + static_cast<std::int64_t>((a >> 32) %
                                              static_cast<std::uint64_t>(width));
            const bool is_bid = ((a >> 20) & 1) != 0;
            const bool del = ((a >> 21) % 10) < 3;
            const std::int64_t qty =
                del ? 0 : 1 + static_cast<std::int64_t>(a % 1000000);
            updates.push_back({is_bid ? kMid - off : kMid + off, qty, is_bid});
        }
        MapBook map_book;
        std::int64_t sum_map = 0;
        const Stats m = bench(warmup, batches, batch, [&](std::size_t i) {
            map_book.apply(updates[i]);
            sum_map += map_book.best_bid() + map_book.best_ask();
        });
        LadderBook ladder(kMid - width - 1, 2 * width + 2);
        std::int64_t sum_ladder = 0;
        const Stats l = bench(warmup, batches, batch, [&](std::size_t i) {
            ladder.apply(updates[i]);
            sum_ladder += ladder.best_bid() + ladder.best_ask();
        });
        if (sum_map != sum_ladder) {
            std::fprintf(stderr, "book mismatch at width %lld\n",
                         static_cast<long long>(width));
            std::exit(EXIT_FAILURE);
        }
        std::printf("%lld,%zu,%zu,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n",
                    static_cast<long long>(width), map_book.levels(),
                    ladder.bytes(), m.p50, m.p95, m.p99, l.p50, l.p95, l.p99);
    }
}

// ------------------------------------------------------------------
// Example 2: parsing a depth-update message
// ------------------------------------------------------------------
//
// The mock messages follow the field layout of the public "diff. depth"
// stream documented by Binance (event type, event time, symbol, first and
// final update ID, bids, asks). They are generated locally.

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

// Baseline: general-purpose string handling. Each field is copied into a
// temporary std::string and converted to double, and the result is
// returned in a std::vector that grows on the heap. Two conversions are
// provided because their cost differs a lot between standard libraries.
struct LevelD {
    double price;
    double qty;
    bool is_bid;
};

double to_double_stod(const std::string& s) { return std::stod(s); }

double to_double_from_chars(const std::string& s) {
    double d = 0.0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), d);
    if (r.ec != std::errc()) throw std::runtime_error("bad number");
    return d;
}

std::vector<LevelD> parse_naive(const std::string& msg,
                                double (*to_double)(const std::string&)) {
    std::vector<LevelD> out;
    for (const char* key : {"\"b\":[", "\"a\":["}) {
        std::size_t pos = msg.find(key);
        if (pos == std::string::npos)
            throw std::runtime_error("side array not found");
        pos += 5;
        while (pos < msg.size() && msg[pos] == '[') {
            const std::size_t p1 = msg.find('"', pos);
            const std::size_t p2 = msg.find('"', p1 + 1);
            const std::size_t q1 = msg.find('"', p2 + 1);
            const std::size_t q2 = msg.find('"', q1 + 1);
            if (p1 == std::string::npos || p2 == std::string::npos ||
                q1 == std::string::npos || q2 == std::string::npos)
                throw std::runtime_error("malformed level");
            const std::string price = msg.substr(p1 + 1, p2 - p1 - 1);
            const std::string qty = msg.substr(q1 + 1, q2 - q1 - 1);
            out.push_back({to_double(price), to_double(qty), key[1] == 'b'});
            pos = q2 + 2;  // skip the closing quote and bracket
            if (pos < msg.size() && msg[pos] == ',') ++pos;
        }
    }
    return out;
}

// --- snippet: parser ---
// Reads a decimal such as 67250.10 as an integer scaled by
// 10^decimals. Extra fractional digits are dropped. Overflow
// is not checked, which is acceptable for bounded prices.
bool parse_fixed(const char*& p, const char* end, int decimals,
                 std::int64_t& out) {
    const char* const start = p;
    std::int64_t v = 0;
    int frac = -1;  // -1 until the decimal point is seen
    for (; p < end; ++p) {
        const char c = *p;
        if (c >= '0' && c <= '9') {
            if (frac == decimals) continue;
            if (frac >= 0) ++frac;
            v = v * 10 + (c - '0');
        } else if (c == '.' && frac < 0) {
            frac = 0;
        } else {
            break;
        }
    }
    if (p == start) return false;
    for (int f = frac < 0 ? 0 : frac; f < decimals; ++f) v *= 10;
    out = v;
    return true;
}

bool skip(const char*& p, const char* end, std::string_view lit) {
    if (static_cast<std::size_t>(end - p) < lit.size() ||
        std::string_view(p, lit.size()) != lit)
        return false;
    p += lit.size();
    return true;
}

// Optimized: one pass over the bytes of the message, output
// written into a caller-provided array, no heap allocation.
// Returns the number of levels, or -1 on malformed input.
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
// --- end snippet ---

void run_parsing() {
    const int per_side = 5;
    const std::size_t distinct = 4096;
    const auto msgs = make_messages(distinct, per_side);

    // Correctness: both parsers must agree on every message.
    for (const std::string& m : msgs) {
        const auto a = parse_naive(m, to_double_from_chars);
        if (parse_naive(m, to_double_stod).size() != a.size()) std::exit(1);
        Update b[32];
        const int n = parse_fast(m, b, 32);
        bool ok = n == static_cast<int>(a.size()) && n == 2 * per_side;
        for (int k = 0; ok && k < n; ++k) {
            ok = std::llround(a[k].price * 100.0) == b[k].price &&
                 std::llround(a[k].qty * 1e8) == b[k].qty &&
                 a[k].is_bid == b[k].is_bid;
        }
        if (!ok) {
            std::fprintf(stderr, "parser mismatch\n");
            std::exit(EXIT_FAILURE);
        }
    }

    const std::size_t warmup = 1000, batches = 10000, batch = 50;
    double sink_d = 0.0;
    std::int64_t sink_i = 0;

    const Stats s_stod = bench(warmup, batches, batch, [&](std::size_t i) {
        const auto v = parse_naive(msgs[i % distinct], to_double_stod);
        sink_d += v.front().price + static_cast<double>(v.size());
    });
    const Stats s_naive = bench(warmup, batches, batch, [&](std::size_t i) {
        const auto v = parse_naive(msgs[i % distinct], to_double_from_chars);
        sink_d += v.front().price + static_cast<double>(v.size());
    });
    const Stats s_fast = bench(warmup, batches, batch, [&](std::size_t i) {
        Update buf[32];
        const int n = parse_fast(msgs[i % distinct], buf, 32);
        sink_i += buf[0].price + n;
    });

    // The same two parsers followed by the matching book from Example 1.
    MapBook map_book;
    const Stats s_pipe_naive =
        bench(warmup, batches, batch, [&](std::size_t i) {
            for (const LevelD& l :
                 parse_naive(msgs[i % distinct], to_double_from_chars)) {
                map_book.apply({std::llround(l.price * 100.0),
                                std::llround(l.qty * 1e8), l.is_bid});
            }
            sink_i += map_book.best_bid() + map_book.best_ask();
        });
    LadderBook ladder(kBase, kLevels);
    const Stats s_pipe_fast =
        bench(warmup, batches, batch, [&](std::size_t i) {
            Update buf[32];
            const int n = parse_fast(msgs[i % distinct], buf, 32);
            for (int k = 0; k < n; ++k) ladder.apply(buf[k]);
            sink_i += ladder.best_bid() + ladder.best_ask();
        });

    std::printf("Example 2: depth message with %d levels (ns per message)\n",
                2 * per_side);
    report("parse: std::string + stod", s_stod);
    report("parse: std::string + from_chars", s_naive);
    report("parse: zero-copy fixed-point", s_fast);
    report("parse + std::map book", s_pipe_naive);
    report("parse + price ladder", s_pipe_fast);
    std::printf("  message length: %zu bytes, checksum: %.0f\n\n",
                msgs[0].size(), sink_d + static_cast<double>(sink_i));
}

// ------------------------------------------------------------------
// Example 3: handing updates from a network thread to a strategy thread
// ------------------------------------------------------------------

// Baseline: std::queue protected by one mutex. Safe for any number of
// producers and consumers. Every push and pop takes the lock, and the
// queue allocates memory as it grows.
template <class T>
class MutexQueue {
public:
    bool try_push(const T& v) {
        std::lock_guard<std::mutex> lock(m_);
        q_.push(v);
        return true;
    }
    bool try_pop(T& v) {
        std::lock_guard<std::mutex> lock(m_);
        if (q_.empty()) return false;
        v = q_.front();
        q_.pop();
        return true;
    }

private:
    std::mutex m_;
    std::queue<T> q_;
};

// --- snippet: spsc ---
// Bounded ring buffer for exactly ONE producer thread and ONE
// consumer thread. Only the producer writes tail_ and only the
// consumer writes head_, so no lock is needed. It is not safe
// with more than one thread on either side.
template <class T>
class SpscRing {
public:
    explicit SpscRing(std::size_t capacity_pow2)
        : mask_(capacity_pow2 - 1), buf_(capacity_pow2) {
        if (capacity_pow2 == 0 || (capacity_pow2 & mask_) != 0)
            throw std::invalid_argument("capacity: power of two");
    }
    bool try_push(const T& v) {  // producer thread only
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t - head_.load(std::memory_order_acquire) > mask_)
            return false;        // full: caller decides what to do
        buf_[t & mask_] = v;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
    bool try_pop(T& v) {         // consumer thread only
        const std::size_t h = head_.load(std::memory_order_relaxed);
        if (h == tail_.load(std::memory_order_acquire))
            return false;        // empty
        v = buf_[h & mask_];
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

private:
    const std::size_t mask_;
    std::vector<T> buf_;
    // Separate cache lines so the two threads do not invalidate
    // each other's index on every operation (false sharing).
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};
// --- end snippet ---

// Sends n updates from a producer thread to the calling thread and
// returns the wall-clock time per message in ns. Both sides spin instead
// of sleeping, so each thread keeps one core busy for the whole run.
template <class Queue>
double run_queue_once(Queue& q, const std::vector<Update>& updates,
                      std::int64_t& checksum) {
    const std::size_t n = updates.size();
    const auto t0 = Clock::now();
    std::thread producer([&] {
        for (std::size_t i = 0; i < n; ++i) {
            while (!q.try_push(updates[i])) {
            }
        }
    });
    std::int64_t sum = 0;
    Update u{};
    for (std::size_t got = 0; got < n;) {
        if (q.try_pop(u)) {
            sum += u.price;
            ++got;
        }
    }
    producer.join();
    const auto t1 = Clock::now();
    checksum = sum;
    return std::chrono::duration<double, std::nano>(t1 - t0).count() /
           static_cast<double>(n);
}

void run_queues() {
    const std::size_t n = 2000000;
    const int runs = 9;
    const auto updates = make_updates(n);
    std::vector<double> t_mutex, t_spsc;
    std::int64_t c1 = 0, c2 = 0;
    for (int r = 0; r < runs; ++r) {
        MutexQueue<Update> mq;
        t_mutex.push_back(run_queue_once(mq, updates, c1));
        SpscRing<Update> ring(1 << 16);
        t_spsc.push_back(run_queue_once(ring, updates, c2));
    }
    std::sort(t_mutex.begin(), t_mutex.end());
    std::sort(t_spsc.begin(), t_spsc.end());
    std::printf("Example 3: thread hand-off, %zu messages, %d runs "
                "(ns per message)\n", n, runs);
    std::printf("  %-34s %9s %9s %9s\n", "", "median", "min", "max");
    std::printf("  %-34s %9.1f %9.1f %9.1f\n", "std::queue + std::mutex",
                t_mutex[runs / 2], t_mutex.front(), t_mutex.back());
    std::printf("  %-34s %9.1f %9.1f %9.1f\n", "SPSC ring buffer",
                t_spsc[runs / 2], t_spsc.front(), t_spsc.back());
    std::printf("  checksums identical: %s\n\n", c1 == c2 ? "yes" : "NO");
    if (c1 != c2) std::exit(EXIT_FAILURE);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--scaling") == 0) {
        run_scaling();
        return EXIT_SUCCESS;
    }
    std::printf("latency_examples.cpp | compiler: %s | "
                "hardware threads: %u\n", __VERSION__,
                std::thread::hardware_concurrency());
    std::printf("steady_clock smallest observed step: %.0f ns\n\n",
                clock_step_ns());
    std::printf("  %-34s %9s %9s %9s %9s\n", "", "p50", "p95", "p99",
                "mean");
    try {
        run_order_book();
        run_parsing();
        run_queues();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

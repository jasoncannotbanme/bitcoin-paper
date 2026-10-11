/* latency_examples.c
 *
 * Illustrative C17 examples for the paper "Reducing Latency in
 * High-Frequency Bitcoin Trading: A Literature Review".
 *
 *   Example 4a: a monotonic clock, its resolution and its own cost
 *   Example 4b: per-message malloc/free versus one reused buffer
 *   Example 4c: strtod versus a fixed-point decimal parser
 *
 * Build:  gcc -std=c17 -O2 latency_examples.c -o latency_examples_c
 * Run:    ./latency_examples_c
 *
 * All data is synthetic. The program does not use the network.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 199309L /* for clock_gettime */
#endif

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

/* --- snippet: clock --- */
/* Monotonic time in nanoseconds. A monotonic clock never jumps
 * backwards when the system time is adjusted, which a wall
 * clock can do. */
static uint64_t now_ns(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    const uint64_t f = (uint64_t)freq.QuadPart;
    const uint64_t t = (uint64_t)c.QuadPart;
    return (t / f) * 1000000000u + (t % f) * 1000000000u / f;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
#endif
}

static int cmp_double(const void *a, const void *b) {
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Nearest-rank percentile of a sorted array, 0 < p <= 1. */
static double percentile(const double *sorted, size_t n, double p) {
    size_t rank = (size_t)(p * (double)n);
    if ((double)rank < p * (double)n) ++rank; /* ceiling */
    return sorted[rank == 0 ? 0 : rank - 1];
}

typedef void (*op_fn)(size_t i, void *ctx);

/* Times op in batches. One sample is the mean time per call in
 * one batch, because one call can be shorter than a clock step.
 * Prints the median, 95th and 99th percentile of the samples. */
static void bench(const char *name, size_t warmup, size_t batches,
                  size_t batch, op_fn op, void *ctx) {
    double *ns = malloc(batches * sizeof *ns);
    if (ns == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    size_t i = 0;
    for (size_t b = 0; b < warmup + batches; ++b) {
        const uint64_t t0 = now_ns();
        for (size_t k = 0; k < batch; ++k) op(i++, ctx);
        const uint64_t t1 = now_ns();
        if (b >= warmup)
            ns[b - warmup] = (double)(t1 - t0) / (double)batch;
    }
    qsort(ns, batches, sizeof *ns, cmp_double);
    printf("  %-34s %9.1f %9.1f %9.1f\n", name,
           percentile(ns, batches, 0.50),
           percentile(ns, batches, 0.95),
           percentile(ns, batches, 0.99));
    free(ns);
}
/* --- end snippet --- */

/* ---------------------------------------------------------------- */
/* Example 4a: what the clock itself can and cannot resolve          */
/* ---------------------------------------------------------------- */

static void run_clock(void) {
    uint64_t step = UINT64_MAX;
    for (int i = 0; i < 200000; ++i) {
        const uint64_t a = now_ns();
        uint64_t b = now_ns();
        while (b == a) b = now_ns();
        if (b - a < step) step = b - a;
    }
    const size_t n = 5000000;
    volatile uint64_t sink = 0;
    const uint64_t t0 = now_ns();
    for (size_t i = 0; i < n; ++i) sink += now_ns();
    const uint64_t t1 = now_ns();
    (void)sink;
    printf("Example 4a: monotonic clock\n");
    printf("  smallest observed step: %" PRIu64 " ns\n", step);
    printf("  mean cost of one call:  %.1f ns\n\n",
           (double)(t1 - t0) / (double)n);
}

/* ---------------------------------------------------------------- */
/* Example 4b: buffer handling for incoming messages                 */
/* ---------------------------------------------------------------- */

enum { MSG_COUNT = 1024, MSG_CAP = 512 };

struct messages {
    char text[MSG_COUNT][MSG_CAP];
    size_t len[MSG_COUNT];
    char *reused;      /* one buffer allocated before the loop */
    uint64_t checksum;
};

static uint64_t rng_state = 88172645463325252u;
static uint64_t rng_next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static void make_messages(struct messages *m) {
    for (size_t k = 0; k < MSG_COUNT; ++k) {
        int n = snprintf(m->text[k], MSG_CAP,
                         "{\"e\":\"depthUpdate\",\"E\":%" PRIu64
                         ",\"s\":\"BTCUSDT\",\"U\":%zu,\"u\":%zu,\"b\":[",
                         (uint64_t)1700000000000u + k, 10 * k + 1,
                         10 * k + 10);
        for (int side = 0; side < 2; ++side) {
            for (int j = 0; j < 5; ++j) {
                const uint64_t r = rng_next();
                n += snprintf(m->text[k] + n, MSG_CAP - (size_t)n,
                              "%s[\"%u.%02u\",\"%u.%08u\"]", j ? "," : "",
                              67000u + (unsigned)(r % 500),
                              (unsigned)((r >> 16) % 100),
                              (unsigned)((r >> 24) % 10),
                              (unsigned)((r >> 32) % 100000000u));
            }
            n += snprintf(m->text[k] + n, MSG_CAP - (size_t)n, "%s",
                          side == 0 ? "],\"a\":[" : "]}");
        }
        if (n < 0 || n >= MSG_CAP) {
            fprintf(stderr, "message buffer too small\n");
            exit(EXIT_FAILURE);
        }
        m->len[k] = (size_t)n;
    }
}

/* Stand-in for "process the message": touch every byte once. */
static uint64_t sum_bytes(const char *p, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; ++i) s += (unsigned char)p[i];
    return s;
}

/* Baseline: a fresh heap block for every message. */
static void op_malloc(size_t i, void *ctx) {
    struct messages *m = ctx;
    const size_t k = i % MSG_COUNT;
    char *copy = malloc(m->len[k]);
    if (copy == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    memcpy(copy, m->text[k], m->len[k]);
    m->checksum += sum_bytes(copy, m->len[k]);
    free(copy);
}

/* Optimized: the same copy into a buffer that is reused. */
static void op_reuse(size_t i, void *ctx) {
    struct messages *m = ctx;
    const size_t k = i % MSG_COUNT;
    memcpy(m->reused, m->text[k], m->len[k]);
    m->checksum += sum_bytes(m->reused, m->len[k]);
}

/* ---------------------------------------------------------------- */
/* Example 4c: decimal text to a number                              */
/* ---------------------------------------------------------------- */

struct prices {
    char text[MSG_COUNT][16];
    double sum_d;
    int64_t sum_i;
};

/* Reads a decimal such as 67250.10 as an integer scaled by
 * 10^decimals. Returns 0 on success and -1 if no digit was found. */
static int parse_fixed(const char *p, int decimals, int64_t *out) {
    const char *start = p;
    int64_t v = 0;
    int frac = -1;
    for (;; ++p) {
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
    if (p == start) return -1;
    for (int f = frac < 0 ? 0 : frac; f < decimals; ++f) v *= 10;
    *out = v;
    return 0;
}

static void op_strtod(size_t i, void *ctx) {
    struct prices *pr = ctx;
    char *end = NULL;
    const double d = strtod(pr->text[i % MSG_COUNT], &end);
    if (end == pr->text[i % MSG_COUNT]) exit(EXIT_FAILURE);
    pr->sum_d += d;
}

static void op_fixed(size_t i, void *ctx) {
    struct prices *pr = ctx;
    int64_t v = 0;
    if (parse_fixed(pr->text[i % MSG_COUNT], 2, &v) != 0)
        exit(EXIT_FAILURE);
    pr->sum_i += v;
}

int main(void) {
    printf("latency_examples.c | compiler: %s\n\n", __VERSION__);
    run_clock();

    struct messages *m = calloc(1, sizeof *m);
    struct prices *pr = calloc(1, sizeof *pr);
    if (m == NULL || pr == NULL) {
        perror("calloc");
        return EXIT_FAILURE;
    }
    make_messages(m);
    m->reused = malloc(MSG_CAP);
    if (m->reused == NULL) {
        perror("malloc");
        return EXIT_FAILURE;
    }
    for (size_t k = 0; k < MSG_COUNT; ++k) {
        const uint64_t r = rng_next();
        snprintf(pr->text[k], sizeof pr->text[k], "%u.%02u",
                 67000u + (unsigned)(r % 500), (unsigned)((r >> 16) % 100));
    }

    printf("  %-34s %9s %9s %9s\n", "", "p50", "p95", "p99");
    printf("Example 4b: copy + scan one message of %zu bytes "
           "(ns per message)\n", m->len[0]);
    bench("malloc + copy + free (baseline)", 1000, 10000, 100, op_malloc, m);
    const uint64_t c1 = m->checksum;
    m->checksum = 0;
    bench("copy into reused buffer", 1000, 10000, 100, op_reuse, m);
    printf("  checksums identical: %s\n\n", c1 == m->checksum ? "yes" : "NO");

    printf("Example 4c: one price string to a number (ns per call)\n");
    bench("strtod (baseline)", 1000, 10000, 100, op_strtod, pr);
    bench("fixed-point parser", 1000, 10000, 100, op_fixed, pr);
    printf("  sums: %.2f and %.2f\n", pr->sum_d, (double)pr->sum_i / 100.0);

    const int ok = (c1 == m->checksum);
    free(m->reused);
    free(m);
    free(pr);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

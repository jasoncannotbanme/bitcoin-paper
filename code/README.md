# Latency examples

Illustrative code for the paper "Reducing Latency in Proprietary Cryptocurrency
High-Frequency Trading: A Literature Review with Empirical Evidence" (Section 5).

All market data is generated inside the programs. They open no network
connection, need no API credentials, and cannot place orders.

## Files

| File | Language | Contents |
|---|---|---|
| `latency_examples.c` | C17 | Monotonic clock and batch timing; clock resolution and cost; malloc per message versus a reused buffer; `strtod` versus a fixed-point parser |
| `latency_examples.cpp` | C++20 | Order book with `std::map` versus a flat price ladder; depth-message parsing, allocating versus zero-copy; mutex queue versus a single-producer single-consumer ring buffer |
| `advanced_examples.cpp` | C++20 | Fixed-block memory pool versus malloc; full pipeline benchmark (parse→book→signal); burst-processing throughput test |
| `benchmark_output.txt` | text | Raw output of the five runs reported in the paper (Table 3, Fig. 1) |
| `scaling_output.csv` | CSV | Five runs of the book-depth test, `latency_examples_cpp --scaling` (Fig. 2) |
| `make_figures.py` | Python | Draws the three figures of the paper into `../figures` (needs matplotlib) |

The folder `../figures` holds the three PNG figures and `fig2_data.csv`, the
per-depth medians plotted in Fig. 2.

## Requirements

- A C17 compiler and a C++20 compiler. Tested with GCC 16.1.0 (MinGW-w64, UCRT)
  on Windows 10.
- The C++ file uses floating-point `std::from_chars` from `<charconv>`, which
  needs GCC 11 or newer, MSVC 2019 16.4 or newer, or a recent libc++.
- No third-party libraries. The C file uses `QueryPerformanceCounter` on
  Windows and `clock_gettime(CLOCK_MONOTONIC)` elsewhere. The non-Windows path
  was not compiled or run for the paper.

## Build

```sh
gcc -std=c17   -O2 latency_examples.c   -o latency_examples_c
g++ -std=c++20 -O2 -pthread latency_examples.cpp -o latency_examples_cpp
g++ -std=c++20 -O2 -pthread advanced_examples.cpp -o advanced_examples
```

## Run

```sh
./latency_examples_c
./latency_examples_cpp
```

On Windows the programs are `latency_examples_c.exe` and
`latency_examples_cpp.exe`. Each program takes no arguments and finishes in
well under a minute. It exits with a non-zero status if the baseline and
optimized variants disagree on their results.

## Reproducing the numbers in the paper

1. Close other heavy programs. The paper's runs used an ordinary desktop
   session with no CPU pinning.
2. Run each program five times and take the median of each printed statistic:

   ```sh
   for i in 1 2 3 4 5; do ./latency_examples_c; ./latency_examples_cpp; done > out.txt
   ```

3. Compare with `benchmark_output.txt`.
4. For the book-depth test and the figures:

   ```sh
   for i in 1 2 3 4 5; do echo "# run $i"; ./latency_examples_cpp --scaling; done > scaling_output.csv
   python make_figures.py
   ```

   Figures 1 and 3 are drawn from the values printed in the paper, which are
   written into `make_figures.py`. Update them there if you re-run the
   benchmarks and want the figures to show your own numbers.

The workload uses a fixed random seed, so the input is identical on every run.
The timings are not: they depend on the processor, compiler, standard
library, operating system, and whatever else the machine is doing. Expect
different absolute numbers on a different machine.

## How to read the output

- Times are nanoseconds per operation. Columns are the median (p50), the 95th
  percentile (p95), and the 99th percentile (p99).
- Operations are timed in batches of 50 or 100 because one operation can be
  shorter than one step of the clock (100 ns on the test machine). Each sample
  is the mean of one batch, so a single slow call inside a batch is averaged
  out and the true per-call tail is understated.
- The thread hand-off test reports the mean time per message over a whole run
  (median, minimum, and maximum of nine runs), not percentiles. Both threads
  spin, so the test keeps two cores fully busy.
- These are local processing times only. They say nothing about network round
  trips, exchange processing, or blockchain settlement.

## Known limits of the examples

- The price ladder covers a fixed window of 4,096 ticks and does not
  re-center. The synthetic workload keeps the mid price fixed.
- The fast parser handles only the message layout generated here, which
  follows the field layout of Binance's documented diff. depth stream. It is
  not a general JSON parser.
- The ring buffer is correct only with exactly one producer thread and one
  consumer thread.

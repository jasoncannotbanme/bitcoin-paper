# tasks.md - Semester Assignment Todo & Checklist

### 8 Oct
- [x] Assignment announced in class (Literature review paper using Springer template).
- [x] Pick topic: Crypto HFT latency & why software speed matters even with 2ms cloud ping.
- [x] Download Springer CCIS Word template (`springer_template-ICMMCS-2025.docx`).
- [x] Search for papers on Google Scholar & arXiv:
  - [x] Find Bybit/Binance empirical latency papers -> found Albers et al. 2025.
  - [x] Moallemi 2013 for cost of latency formula.
  - [x] Makarov 2020 & Hautsch 2024 for crypto arbitrage limits.

### 10 Oct
- [x] Outline the 5 latency stages (Network -> OS socket -> JSON parse -> Book update -> Queue).
- [x] Set up Representative Firm profile on AWS Tokyo (`ap-northeast-1`).
- [ ] ~~Test live Binance WebSockets from home~~ -> ping jitter is 80ms, useless for benchmarks. Decided to do deterministic offline replay instead.

### 11 Oct
- [x] Write C timer harness (`latency_examples.c`).
- [x] Fix MinGW linker error (need `-lkernel32` for `QueryPerformanceCounter`).
- [x] Fix 0.0 ns timer bug: QPC resolution is 100ns, must time in batches of 50 or 100 calls!
- [x] Implement fast fixed-point price parser (`parse_price_fixed`) to replace `strtod`.

### 12-13 Oct
- [x] Write C++ order book test (`latency_examples.cpp`).
- [x] Benchmark `std::map<double, double>` vs flat array `PriceLadder`.
- [x] Fix GCC `-O2` dead code elimination deleting the ladder loop (add volatile checksum sink).
- [x] Write SPSC ring buffer for thread hand-off.
- [x] Fix false sharing bug in SPSC queue! `head` and `tail` were in same cache line (138ns -> 28ns with `alignas(64)`).
- [x] Run scaling sweep up to 262,144 price levels. Save to `scaling_output.csv`.
- [x] Fix segfault when price drifted outside 4096-tick array bounds.

### 15-16 Oct
- [x] Start rough draft in Word (`NOTFINAL_draft.docx`).
- [x] Fill in Table 1 (latency breakdown) and Table 3-6 (benchmark results).
- [x] Look up Chilimbi 1999 paper to explain why red-black tree pointer chasing causes L3 cache misses.

### 17-18 Oct
- [x] Write `advanced_examples.cpp`:
  - [x] Test custom fixed-block memory pool vs malloc. (Didn't beat malloc because of Windows LFH, keep the honest 108ns result in Table 7).
  - [x] Full pipeline benchmark (parse -> book update -> signal).
  - [x] Burst throughput test: 500 depth messages arriving in 10ms.
- [x] Calculate burst queue drain math: 500 * 2.86us = 1.43ms backlog for naive code vs 240us for fast code. (This answers Ma'am's question!).

### 20-21 Oct
- [x] Ask Claude to verify all 17 reference DOIs and format citations in Springer CCIS style [1]..[17].
- [x] Fix author list on Albers 2025 (add Shestopaloff).
- [x] Claude wrote matplotlib plotting script (`make_new_figures.py`).
- [x] Generate all 6 figures at 300 DPI into `figures/`.
- [x] Add comments and review highlights into `NOTFINAL_draft.docx`.

### 22-23 Oct
- [x] Read through `NOTFINAL_draft.docx` and polish text for final submission.
- [x] Compile clean final manuscript (`Reducing_Latency_HF_Bitcoin_Trading_Literature_Review.docx`).
- [x] Export final PDF.
- [x] Clean up repo and push to GitHub.

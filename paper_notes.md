# paper_notes.md

albers 2025 paper (bybit vs binance):
"We find that one-way network propagation between cloud infrastructure in Tokyo and exchange order matching engines exhibits a non-stationary baseline of approximately 1.8 ms to 2.4 ms, with tail excursions exceeding 15 ms during volatility spikes."
-> THIS IS THE WHOLE PREMISE. 
Nobody has physical exchange colocation in crypto. AWS Tokyo (ap-northeast-1) is the best you get.
Wait if ping is 2ms why do prop shops care about 50ns in software?
-> AWS latency is a shared floor. Everyone gets ~2ms. The edge is in the software queue.

moallemi 2013 cost of latency:
C = 0.5 * lambda * sigma^2 * tau
tau = latency
sigma = volatility
crypto volatility during liquidation cascade is 5x equity markets. so even if tau is small, cost explodes.
stale quote problem: if binance dumps and your cancel order on bybit is 100us late, taker bot snipes your bid. adverse selection.

makarov & schoar (2020) vs hautsch (2024):
makarov says arbitrage spreads used to last hours across fiat exchanges (korea kimchi premium).
hautsch 2024 explains why you cant arbitrage on-chain: bitcoin 10 min block time is way too slow. price moves 20 times before 1 confirmation.
so prop firms do SYNTHETIC arbitrage: keep pre-funded USDT margin on both binance and bybit, trade off-chain via websocket APIs.
everything comes down to off-chain api execution speed.

why fpga is overrated for crypto (dvorak 2014, lockwood 2012):
dvorak built 15ns fpga book for nasdaq itch.
itch = raw UDP multicast.
binance/bybit = TLS 1.3 over WebSockets over TCP!
decrypting TLS in fpga hardware needs custom IP cores, costs $50k+, breaks whenever binance updates endpoints.
for mid-tier prop firm: bare-metal linux + C++20 + kernel bypass is 90% as fast at 0 cost. put this in section 5.

chilimbi 1999 (cache conscious data structures):
std::map in C++ is a red-black tree.
each node = left + right + parent + color + data = 48 bytes on heap.
traversing 2000 levels chases 11 pointers across random heap memory -> L3 cache miss every single time (40-60ns stall).
flat price ladder: uint64 array indexed by (price - base) / tick.
contiguous in RAM -> fits in L1 cache line (64 bytes). prefetcher loads adjacent levels before cpu even asks.
ladder update is flat at 8ns while map slows down to 600ns at 300k depth.

michael & scott 1996:
non-blocking concurrent queue.
mutex = os kernel context switch (~1000ns).
lock-free SPSC ring buffer = atomic head + tail with acquire/release memory fences.
(make sure head and tail are on different cache lines or else cores fight over memory bus!)

THE BURST QUEUE ARGUMENT (need to make sure ma'am understands this):
if ping is 2,000,000 ns, why care about 1,000 ns?
market data comes in bursts. 500 depth messages arrive in 10ms when price drops.
if code takes 2.5us per message:
500 * 2.5us = 1.25ms queue backlog!
by message 400 your bot is looking at an orderbook that is 1 millisecond OLD.
you quote on old prices and get wrecked.
with 500ns fast code:
burst clears in 240us. max queue is 7 messages. you stay on top of the book.

citations stuff:
asked claude to clean up the bibtex and pull the DOIs for all 17 references so i don't have to look up each one on google scholar.
albers had missing shestopaloff in author list, fixed.
format for springer ccis: numbered [1] to [17] with full titles and doi links.

binance diff depth payload:
b = bids, a = asks.
prices are strings: "67420.50".
strtod does floating point division, exponent parsing, locale check = 124ns.
fixed point integer parsing: 6742050 = 11ns. zero floating point rounding errors.

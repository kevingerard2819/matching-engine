# Single-symbol matching engine

A C++17, single-threaded limit order book with Linux UDP ingress, a bounded feed-to-engine queue, and reproducible performance experiments. No external C++ dependencies. The direct replay path remains the comparison baseline.

## Build and test on Linux

Install a C++17 compiler and CMake, then run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/replay data/example.orders
```

The example prints a trade of 40 shares at 1000 ticks, with one sell order remaining for 60 shares. Its summary is `commands=2 trades=1 resting_orders=1 cancelled=0`.

For memory and undefined-behavior checks:

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure
```

To reproduce Release checks, checked STL Debug checks, and repeated benchmark comparisons together:

```sh
sh scripts/validate-linux.sh
```

This requires CMake, Ninja, a C++17 compiler, and Python 3. `BUILD_JOBS`, `BENCH_ORDERS`, and `BENCH_RUNS` control resource use. The script probes address/undefined sanitizer support and records the result in `build-release/sanitizer-status.txt`; if the toolchain lacks those runtimes, it explicitly reports that only checked STL Debug tests ran. Use `SANITIZERS=ON` to require sanitizers rather than allowing that fallback. Checked STL flags apply to GCC/libstdc++.

Tests cover full and partial fills, both sides' price and FIFO priority, fills across levels, limit boundaries, cancellations, empty levels, rejected input, live duplicate IDs, ID reuse, integer boundaries, and 5,000 deterministic operations compared against an independent scanning reference book. Feed tests cover binary encoding, malformed packets, sequence decisions, queue saturation, wraparound, concurrent FIFO and shutdown. Linux loopback integration tests compare actual UDP executions with replay and exercise duplicate, gap, publisher, and oversized-packet handling. Python 3 enables integration tests automatically. Checks remain active in Release builds. Linux CI runs GCC and Clang in Release and sanitized Debug configurations.

## Rules and representation

- Every order has a positive 64-bit ID, side, positive signed 64-bit tick price, positive unsigned 64-bit quantity, and engine-assigned arrival sequence. At a one-cent tick size, 1005 means $10.05. No monetary floating-point arithmetic is used.
- Bids sort highest first; asks sort lowest first. Each price level is a FIFO list. Partial fills retain their arrival position.
- An incoming order consumes crossing orders at the best opposite price, oldest first. Executions use the **resting order's price**. Any unfilled incoming quantity rests at its limit price.
- Only live IDs must be unique. Rejected input consumes no arrival sequence. Fully filled or cancelled IDs may be reused; callers needing lifetime uniqueness must enforce it upstream. Unknown cancellations return false.
- Each execution records maker/taker IDs, buy/sell IDs, price, quantity, and trade sequence. Both partial and full executions appear in the in-memory trade log. Replay writes executions to stdout for capture to a file.
- One engine represents one symbol. All calls, including reads, belong to its owner thread. The core contains no sockets or synchronization. The UDP executable owns ingress separately and passes commands through a bounded queue. Fees, order amendments, and persistence/recovery protocols are outside this project.

Price levels use `std::map`, FIFO queues use `std::list`, and an `unordered_map` indexes live IDs to list iterators. Adding a resting order is O(log P) plus average O(1) indexing, where P is the number of price levels. Cancellation is average O(1) for ID lookup plus O(log P) to locate the level. Sweeps additionally visit each consumed order and level. Copying and moving the book are disabled because the ID index contains iterators.

Invalid business input is rejected before mutation. Allocation failures or sequence exhaustion during a sweep are fatal for that engine instance; this baseline does not provide transactional rollback for resource exhaustion. Trade history grows with every execution, so memory use depends on the whole replay, not just live orders.

## File format and replay

```text
# Prices in ticks, quantities in shares; whitespace-delimited.
ADD 1 SELL 1000 100
ADD 2 BUY 1005 40
CANCEL 1
```

Blank lines and `#` comments are supported. Fields are strict, case-sensitive decimal integers: no trailing fields, fractional prices, signs, zero values, or out-of-range values. Syntax errors report a source line; business errors report the replay command number. Unknown cancellations are accepted no-ops.

## Generate and measure a baseline

```sh
./build/generate_orders data/generated.orders 1000000 42
./build/benchmark data/generated.orders
./build/replay data/generated.orders > build/trades.txt
```

The generator writes exactly COUNT commands with unique add IDs, prices from 990 to 1010 ticks, quantities from 1 to 100, mixed sides, and approximately 10% cancellation attempts against previously submitted IDs. Some cancellations therefore target orders already filled or cancelled. The seed makes the file reproducible.

The benchmark loads and validates the whole file **before** timing. It reports processing milliseconds, commands per second, and mean nanoseconds per command. The timed region includes matching, cancellation, result allocation, and retaining the trade log; it excludes parsing, file I/O, and printing. The mean is not a latency percentile. Run several fresh processes on the same input and compare medians. No performance numbers are claimed until a benchmark actually runs.

## Linux UDP feed

Start the receiver, then send from another terminal:

```sh
./build/udp_feed 9000 7 4096 5000 build/udp.trades
./build/udp_sender data/example.orders 127.0.0.1 9000 7 100
```

Receiver arguments: `PORT SYMBOL [CAPACITY=4096] [IDLE_MS=2000] [TRADES_FILE]`. Port zero chooses a free port, printed as `READY port=...`. It binds to loopback for local systems experiments. The first accepted packet pins the source IPv4 address and port for the session; other publishers and symbols are counted and ignored. Restart the receiver for a new publisher session. The idle timer starts at startup and resets only on accepted commands; SIGINT/SIGTERM also trigger shutdown. Shutdown closes the queue, drains the accepted prefix, joins the engine thread, and optionally writes trades in replay's format. Stdout reports acceptance, rejection, sequence, validation and overload counters.

Sender arguments: `FILE IPV4 PORT SYMBOL [INTERVAL_US=100]`. It validates the file before sending and uses monotonically increasing sequences starting at one. An interval of zero sends as fast as possible. UDP provides no acknowledgement or retransmission; `sent=` counts local sends, not accepted commands. A missing final datagram cannot be detected without an expected final sequence, heartbeat or end-of-stream protocol, which this version does not implement. Use accepted counts and trade comparison to establish completeness.

### Wire format, version ME01

Exactly 48 bytes per datagram; all multi-byte fields are unsigned big-endian. Prices must fit positive signed 64-bit ticks. Reserved fields must be zero. Unknown versions, lengths, sides, kinds, or invalid values are rejected before enqueueing.

| Byte offset | Width | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `ME01` |
| 4 | 4 | Positive symbol ID |
| 8 | 8 | Sequence, 1 through UINT64_MAX-1 |
| 16 | 1 | Kind: 1 = ADD, 2 = CANCEL |
| 17 | 1 | ADD side: 1 = BUY, 2 = SELL; CANCEL: zero |
| 18 | 6 | Reserved, zero |
| 24 | 8 | Positive order ID |
| 32 | 8 | ADD tick price; CANCEL: zero |
| 40 | 8 | ADD quantity; CANCEL: zero |

Sequence advances only after successful enqueue. Older sequences are counted as duplicates and ignored. A sequence higher than expected stops ingestion, drains the accepted prefix, and exits nonzero: there is no reordering buffer. Malformed/foreign packets do not advance sequence. If a malformed packet was part of the intended stream, the next valid sequence reveals the gap.

The queue is a preallocated ring protected by a mutex and condition variables. The UDP producer uses nonblocking `try_push`; a full queue stops ingestion and returns nonzero after draining. It does not silently skip a command and continue matching. File benchmarks use blocking `push` for lossless backpressure. There is exactly one matching consumer. Business rejections such as duplicate live IDs consume the feed sequence, increment `rejected`, and cause a nonzero exit after the remaining accepted commands drain. Allocation failures are fatal.

## Queue and allocation experiments

```sh
./build/benchmark data/generated.orders direct
./build/benchmark data/generated.orders queued 4096
./build/benchmark data/generated.orders direct 4096 1000000
python3 bench/experiments.py data/generated.orders --runs 5
python3 bench/experiments.py data/generated.orders --runs 5 --cpus 2,3 --perf
```

The optional reserve count preallocates the live-order hash index and trade vector before timing. It reduces rehashes and vector growth at the cost of startup allocation and memory; it does not preallocate map/list nodes or guarantee a speedup. The queue is also constructed before timing. Queued timing includes thread startup, producer/consumer handoff, draining and joining, so short inputs are dominated by overhead. The matching thread owns the engine exclusively until it joins.

Every benchmark computes a stable digest of all trade fields and final book contents after timing. The experiment driver checks command, trade and live-order counts and that digest against the direct baseline on every run; divergence aborts the experiment. It varies direct/queued mode, queue capacities 1/64/4096, and reservation, producing raw CSV trials, median throughput, and platform/build/workload metadata. Digest equality is a useful regression signal; the rule tests and full UDP trade comparison provide additional verification.

`--cpus` applies `taskset` affinity to the entire process, inherited by both threads; it does not pin each thread separately. Compare unconstrained and pinned runs using available CPU IDs. `--perf` writes counters per trial and requires host perf access. Low queue capacity intentionally exposes synchronization overhead.

Example additional Linux profiling commands:

```sh
taskset -c 2 ./build/benchmark data/generated.orders
perf stat -e cycles,instructions,cache-misses,context-switches \
  ./build/benchmark data/generated.orders
perf record -g -- ./build/benchmark data/generated.orders
perf report
```

`perf` observes the whole process, including file loading; the benchmark's internal timer observes only command processing. Preserve that distinction in comparisons. UDP timings include transport and scheduling and should not be presented as matching-only latency.

For a separate ThreadSanitizer build:

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON
cmake --build build-tsan --parallel
ctest --test-dir build-tsan --output-on-failure
```

Address/undefined and thread sanitizers use separate builds. Subsequent experiments can replace the queue with an SPSC implementation, pool list nodes, or change price-level storage, comparing identical workloads against the retained baseline before claiming an improvement.

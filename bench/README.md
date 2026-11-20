# bench/ (Week 6-7, Pair A)

Benchmark drivers and plotting live here:

- `sweep.sh` — runs every strategy × {1, 2, 4, 8} threads × {calm, busy}, 5 repetitions each, writes `results/sweep.csv`.
- `plot.py` — speedup, efficiency, per-thread busy time and memory graphs from `results/sweep.csv`.

Rules for fair measurements: Release build, same machine, nothing else running, report the median of 5 runs, and record the CPU model and core count with every result file.

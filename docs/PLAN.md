# 8-week plan

Phase 1 = Weeks 1–3 (small, uniform data). Phase 2 = Weeks 4–7 (full, uneven data). Week 8 = report and presentation.

## Week 1: Simple sequential baseline (whole team) ✅ done in this repo
- [x] CMake project, C++20, sanitizer option, unit tests
- [x] `types.hpp` shared interface (to be frozen after team review)
- [x] TSV loader, DOI/title hash maps (C++20 heterogeneous lookup)
- [x] Reference parser for LLM-style lines (DOI, year, quoted title, authors, label)
- [x] Verification ladder: DOI → exact title → brute-force fuzzy (Jaccard on trigrams)
- [x] Report TSV, summary, accuracy, simulated static-chunk imbalance, Amdahl preview
- [x] Synthetic data generator (calm and busy batches)
- [ ] Team review and freeze of `types.hpp` + `Verifier::verify` signature
- **Deliverable:** baseline T(1) per stage on calm and busy batches

## Week 2: First threads (A) · real data and index (B)
- A: `BoundedQueue` (mutex + condition variables), `ThreadPool` with `std::jthread`, static-chunk run on p threads
- A: `ShardedCache` (64 × `shared_mutex`), plugged in as rung 1
- B: `convert_arxiv.py`, 20,000-paper real subset, labelled real/mutated/fake set
- B: sequential `TrigramIndex` (inverted lists + top-K candidates)
- Both: ThreadSanitizer build is clean
- **Deliverable:** first speedup S(p) with static scheduling

## Week 3: Dynamic scheduling (A) · parallel index build (B) — end of Phase 1
- A: dynamic shared queue (atomic next-index counter), per-thread busy-time stats
- B: parallel index build (thread-local indexes + tree merge), barrier before search
- **Deliverable:** static vs dynamic on 1/2/4/8 threads; index build speedup

## Week 4: Merge + full pipeline (whole team) — start of Phase 2
- Pair B's verifier runs inside Pair A's pipeline: reader → queue → workers → reorder buffer → writer
- Full database (1M+ papers): measure load time, the largest sequential cost
- Parallel output == sequential output (automatic diff test)
- **Deliverable:** end-to-end parallel program on real data

## Week 5: Priority (A) · hard references and accuracy (B)
- A: `std::priority_queue` scheduler (no-DOI first), aging against starvation
- A: generator option for clustered fakes (fake refs grouped together, as in real LLM answers)
- B: split hard references across index shards + reduction; tune thresholds; author/Unicode noise
- **Deliverable:** dynamic vs priority on calm/busy; accuracy table v1

## Week 6: Profiling and optimisation (whole team)
- `perf` hot spots, Massif/heaptrack memory, lock contention
- Fixes: false sharing (`alignas(64)`), shard count, top-K size, string allocations
- Optional: work stealing; parallel byte-range DB loading
- **Deliverable:** before/after profile for each fix

## Week 7: Full evaluation
- Sweep: 3–4 strategies × {1, 2, 4, 8} threads × {calm, busy}, 5 runs each (median)
- Graphs: speedup, efficiency, busy time per thread, memory; measured vs Amdahl limit
- **Deliverable:** all graphs + accuracy table final

## Week 8: Report and presentation
- Written report: design, profile, results, qualitative analysis, limits
- Final presentation + live demo; tag `v1.0`, reproducible `run_bench.sh`

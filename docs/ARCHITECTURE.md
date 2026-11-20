# Architecture

## Final (Week 4+) pipeline

```
                    ┌──────────────── startup (once) ────────────────┐
  papers.tsv ──►  Load DB ──► Build trigram index (parallel map) ──► Tree merge ──► barrier
                 [seq]          T0 T1 T2 ... Tp-1 local indexes       [partly seq]
                    └────────────────────────────────────────────────┘
                                                │ read-only from here on
                                                ▼
  refs.txt ─► Reader ─► Bounded queue / ─► Worker pool (p threads) ─► Reorder ─► Writer ─► report.tsv
             [1 thread]  priority queue      each worker runs the       buffer    [1 thread]
              numbers    (scheduler)         verification ladder:      (input
              refs                            1. sharded cache  ◄──┐    order)
                                              2. DOI / title maps  │
                                              3. trigram index ────┘ insert on miss
                                                 + Jaccard top-K
```

## Components and owners

| Component | File | Owner | Synchronisation |
| --- | --- | --- | --- |
| Shared types | `types.hpp` | both | none (plain data) |
| Paper DB + maps | `core/paper_db.*` | B | read-only after load, so no locks |
| Trigram index | `index/trigram_index.hpp` | B | built in parallel (thread-local), merged, then read-only |
| Verifier | `core/verifier.*` | B | `const`, no shared mutable state |
| Bounded queue | `concurrent/bounded_queue.hpp` | A | mutex + 2 condition variables |
| Thread pool | `concurrent/thread_pool.hpp` | A | `std::jthread`, `alignas(64)` per-thread stats |
| Sharded cache | `concurrent/sharded_cache.hpp` | A | 64 × `std::shared_mutex` |
| Scheduler | `scheduling/scheduler.hpp` | A | atomic index (dynamic) / locked priority queue |
| Reorder buffer | `pipeline/reorder_buffer.hpp` | A | mutex + condition variable |

## The interface contract (frozen end of Week 1)

```cpp
Result Verifier::verify(const Reference& ref) const;   // thread-safe, pure w.r.t. shared state
```

Pair A may call this from any number of threads at the same time. Pair B guarantees that `verify` writes only to its return value. From Week 2, the cache is passed in explicitly as a parameter and is not hidden inside the verifier.

## Week 1 (what exists now)

```
Load DB ─► build trigram sets ─► parse refs ─► for each ref: verify() ─► write report
```

All stages run on one thread. Every stage has a `ScopedTimer`, so T(1) and the sequential fraction f are known before any threading work starts.

# citeverify: Parallel LLM Citation Verifier in C++20

citeverify checks the references in an LLM's answer against a local database of real papers (arXiv snapshot, dblp). It gives each reference one of three verdicts: **VERIFIED**, **FOUND_DIFFERS** or **NOT_FOUND** ("not found in our sources", never "fake").

The research goal is parallel-programming work, not a new checker. We measure how scheduling, locking and priority choices affect speedup when the cost per item is very uneven. A reference with a DOI takes ~2 µs. A fake reference forces a full fuzzy search, which takes ~5,500 µs in the Week 1 baseline.

- Proposal: see the project doc ("Proposal: Parallel LLM Citation Verifier in C++")
- Architecture: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- 8-week plan with checklists: [`docs/PLAN.md`](docs/PLAN.md)

## Quick start

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build                       # unit tests

# smoke test on 15 real papers
./build/citeverify --db data/papers_sample.tsv --refs data/refs_sample.txt --out report.tsv

# realistic size: 20k synthetic papers, calm + busy batches of 2,000 references
./scripts/run_bench.sh
```

### Web demo (for presentations)

```bash
python3 web/server.py          # builds the C++ program first if needed
```

Then open **http://localhost:8000** in your browser (from Windows too, when running inside WSL). Paste references or click **Load sample**, then **Check references**. Use **Calm batch** / **Busy batch** for the 2,000-reference benchmark. While the C++ program runs, the page shows live progress, the checking step of each reference and the program's real console output (the server starts it with `--progress`, which prints one line per result). At the end it shows the verdicts, cost per checking step, stage timers, the Amdahl limit and the load-imbalance chart. Tick "Generate new random data" to run a batch on freshly generated data with a new seed. The C++ program does all the checking. `web/server.py` (Python standard library only) just runs it and sends the results to the page. Stop it with `Ctrl+C`.

ThreadSanitizer build (we use it from Week 2, on every commit):

```bash
cmake -B build-tsan -S . -DCITEVERIFY_SANITIZE=thread && cmake --build build-tsan -j
```

## Repository layout

```
citeverify/
├── CMakeLists.txt                  build: core library + CLI + tests, sanitizer option
├── include/citeverify/
│   ├── types.hpp                   ★ shared interface (Paper, Reference, Result, Verdict); both pairs review changes
│   ├── util/timer.hpp              RAII ScopedTimer for stage profiling                       [W1]
│   ├── core/                       Pair B: sequential building blocks
│   │   ├── text.hpp                normalise, trigrams, Jaccard similarity                    [W1]
│   │   ├── paper_db.hpp            load TSV, DOI/title hash maps, trigram sets                [W1]
│   │   ├── reference_parser.hpp    raw LLM reference line -> Reference                        [W1]
│   │   ├── verifier.hpp            verification ladder: cache -> DOI -> title -> fuzzy        [W1]
│   │   └── report.hpp              TSV report, summary, accuracy, imbalance analysis          [W1]
│   ├── index/trigram_index.hpp     Pair B: inverted trigram index, parallel map-reduce build  [W2-3]
│   ├── concurrent/                 Pair A: thread machinery
│   │   ├── bounded_queue.hpp       producer-consumer queue (mutex + condition_variable)       [W2]
│   │   ├── thread_pool.hpp         reusable workers, per-thread busy-time stats              [W2]
│   │   └── sharded_cache.hpp       64 shards × shared_mutex                                   [W2]
│   ├── scheduling/scheduler.hpp    Pair A: static / dynamic / priority / work stealing       [W3-6]
│   └── pipeline/reorder_buffer.hpp Pair A: restore input order before the writer             [W4]
├── src/
│   ├── main.cpp                    CLI, stage timers, Amdahl preview
│   └── core/*.cpp                  implementations of include/citeverify/core
├── tests/test_core.cpp             unit tests (no external framework)
├── scripts/
│   ├── gen_synthetic.py            synthetic papers + labelled calm/busy batches
│   ├── convert_arxiv.py            arXiv JSON snapshot -> TSV                                 [W2]
│   └── run_bench.sh                build + run both batches
├── bench/                          sweep + plotting scripts                                   [W6-7]
├── data/                           small sample DB and reference list (big data is git-ignored)
└── docs/                           ARCHITECTURE.md, PLAN.md
```

Files marked W2+ are **stubs** for now: they contain the planned interface and design notes as comments, so everyone can see where each piece will go.

## Input formats

**Paper database** (TSV): `id <TAB> doi <TAB> title <TAB> authors(;-separated) <TAB> year`

**References**: one per line, written the way an LLM usually writes them. An optional label at the end of the line is used to measure accuracy:

```
A. Vaswani, N. Shazeer et al. "Attention Is All You Need." NeurIPS 2017. doi:10.48550/arXiv.1706.03762  # label=real
```

## Week 1 baseline results (sequential, 20k papers, 2,000 references)

| Batch | Verify time | Fuzzy refs | Avg µs: DOI / exact / fuzzy | Accuracy |
| --- | --- | --- | --- | --- |
| calm (90% real) | 0.79 s | 142 | 2.9 / 1.2 / 5,566 | 97.5% |
| busy (40% real) | 5.1 s | 928 | 4.1 / 2.3 / 5,509 | 80.5% |

Measured in a cloud container. Your numbers will differ, but the ratios should hold.

What Week 1 already shows:
1. **Load imbalance is real.** A fuzzy search costs ~2,000× more than a hash lookup. On the calm batch, the top 10% of references take 99.7% of verification time.
2. **Verification dominates.** The sequential fraction is f ≈ 0.01–0.03, so Amdahl's limit on 8 threads is about 6.6–7.7×.
3. **Accuracy issue to fix in W5.** The synthetic titles use a small vocabulary, so many fake titles score ≥ 0.6 against some real title and get FOUND_DIFFERS. This is a threshold and noise-handling task for Pair B.

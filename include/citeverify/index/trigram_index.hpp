// trigram_index.hpp — Weeks 2-3 (Pair B). STUB: interface sketch only.
//
// Inverted index: trigram -> sorted list of PaperIds whose title contains it.
// A fuzzy query takes the posting lists of its trigrams, counts how often each
// PaperId appears, keeps the top-K candidates, and only those get the exact
// Jaccard score. Millions of comparisons become hundreds.
//
// Week 2: sequential build.
// Week 3: parallel build = map-reduce
//   map:    thread t indexes papers [t*n/p, (t+1)*n/p) into its own local index
//   reduce: tree merge (pairs of local indexes merge, then pairs of results)
// Week 5: "hard" queries split across index shards; each shard returns its
//   best candidate, a reduction picks the global best.
//
//   class TrigramIndex {
//   public:
//       static TrigramIndex build(const PaperDb&, unsigned threads);
//       std::vector<PaperId> candidates(std::span<const uint32_t> query, std::size_t k) const;
//   };
#pragma once

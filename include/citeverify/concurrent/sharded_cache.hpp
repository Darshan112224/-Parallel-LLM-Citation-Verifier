// sharded_cache.hpp — Week 2 (Pair A). STUB: interface sketch only.
//
// Cache: normalised title / DOI  ->  Result of an earlier verification.
// 64 shards, each { std::shared_mutex; StringMap<Result>; }.
//   lookup(key): shard = hash(key) % 64; std::shared_lock  (many readers at once)
//   insert(key): same shard;             std::unique_lock  (one writer, one shard)
// Only threads hitting the SAME shard ever wait for each other.
//
// Experiment (Week 6): 1 shard vs 8 vs 64 shards, mutex vs shared_mutex,
// measured as cache-lookup time under 8 threads.
#pragma once

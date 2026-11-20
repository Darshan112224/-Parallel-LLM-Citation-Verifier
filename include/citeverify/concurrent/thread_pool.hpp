// thread_pool.hpp — Week 2 (Pair A). STUB: interface sketch only.
//
// Fixed set of worker threads created once and reused (thread creation costs
// tens of microseconds — more than a DOI lookup!).
//
// Planned implementation:
//   std::vector<std::jthread> workers_;   // C++20 jthread joins automatically
//                                         // in its destructor and supports
//                                         // cooperative cancellation (stop_token)
//   Each worker loops: task = queue.pop(); if (!task) break; run(task);
//   Each worker records its busy time in a per-thread slot:
//     struct alignas(64) Stats { double busy_ms; std::uint64_t tasks; };
//   alignas(64) puts every slot on its own cache line -> no false sharing.
#pragma once

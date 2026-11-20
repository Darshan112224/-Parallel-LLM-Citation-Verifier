// scheduler.hpp — Weeks 3-5 (Pair A). STUB: interface sketch only.
//
// One interface, several strategies, selected with --schedule on the CLI:
//
//   static    each thread gets an equal contiguous chunk         (Week 3, baseline)
//   dynamic   shared queue; idle threads take the next reference  (Week 3)
//   priority  std::priority_queue; no-DOI refs (likely expensive)  (Week 5)
//             run first, cheap DOI lookups fill the gaps at the end.
//             Aging: a task's priority grows while it waits -> no starvation.
//   stealing  per-thread deques, idle threads steal from the back (Week 6, optional)
//
//   enum class Strategy { Static, Dynamic, Priority, Stealing };
//   std::vector<Result> run(std::span<const Reference>, const Verifier&,
//                           Strategy, unsigned threads, RunStats& stats);
#pragma once

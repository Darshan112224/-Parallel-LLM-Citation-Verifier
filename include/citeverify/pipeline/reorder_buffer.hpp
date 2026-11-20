// reorder_buffer.hpp — Week 4 (Pair A). STUB: interface sketch only.
//
// Workers finish out of order (a cheap reference #7 finishes before an
// expensive #3). The writer must output in input order, so results wait here
// until every earlier index is done.
//
//   std::mutex m_; std::condition_variable cv_;
//   std::map<std::size_t, Result> pending_;  std::size_t next_ = 0;
//   void put(Result r);                       // workers
//   std::vector<Result> take_ready();         // writer: pops next_, next_+1, ...
//
// Dependency: result k is written only after result k-1.
// Watch: memory if one very slow reference holds back thousands of results.
#pragma once

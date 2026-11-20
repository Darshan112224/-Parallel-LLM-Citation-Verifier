// bounded_queue.hpp — Week 2 (Pair A). STUB: interface sketch only.
//
// Producer-consumer queue between the reader thread and the worker pool.
// "Bounded" = fixed capacity: when full, push() blocks, so a fast reader
// cannot fill memory faster than the workers drain it (back-pressure).
//
// Planned implementation:
//   std::mutex m_;  std::condition_variable not_empty_, not_full_;
//   std::deque<T> q_;  std::size_t cap_;  bool closed_ = false;
//
//   void push(T item);            // waits on not_full_
//   std::optional<T> pop();       // waits on not_empty_; nullopt once closed and empty
//   void close();                 // wakes everybody; workers exit their loop
//
// Pitfalls to test: spurious wake-ups (always wait with a predicate),
// lost wake-ups, shutdown while threads are blocked.
#pragma once

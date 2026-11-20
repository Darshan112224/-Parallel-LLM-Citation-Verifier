// report.hpp — write results and print summary statistics.
#pragma once

#include "citeverify/core/paper_db.hpp"
#include "citeverify/types.hpp"

#include <ostream>
#include <span>
#include <vector>

namespace citeverify {

// One TSV line per reference, in input order.
void write_report(std::ostream& out, std::span<const Reference> refs,
                  std::span<const Result> results, const PaperDb& db);

// Verdict counts, accuracy on labelled input, and cost per ladder rung.
void print_summary(std::ostream& out, std::span<const Reference> refs,
                   std::span<const Result> results);

// The Week-1 "why we need dynamic scheduling" analysis: from the measured
// per-reference times, compute how long STATIC equal chunking would take on
// p threads (slowest chunk) versus the perfect-balance ideal (total / p).
void print_imbalance_analysis(std::ostream& out, std::span<const Result> results,
                              std::span<const int> thread_counts);

}  // namespace citeverify

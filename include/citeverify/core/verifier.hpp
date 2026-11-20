// verifier.hpp — the verification ladder for ONE reference (Pair B).
//
// This is the function Pair A's thread pool will call from many threads at
// once, so it is designed to be thread-safe from day one:
//   * verify() is `const` and touches only its arguments and read-only data;
//   * no static / global mutable state.
// Shared mutable state (the cache, Week 2) will be passed in explicitly.
#pragma once

#include "citeverify/core/paper_db.hpp"
#include "citeverify/types.hpp"

namespace citeverify {

struct VerifierConfig {
    double verified_threshold = 0.90;   // title similarity to count as "same paper"
    double candidate_threshold = 0.60;  // below this: NOT_FOUND
    int year_tolerance = 1;             // arXiv year vs. venue year often differ by 1
};

class Verifier {
public:
    explicit Verifier(const PaperDb& db, VerifierConfig cfg = {});

    // Ladder: DOI -> exact title -> fuzzy (brute force in Week 1).
    [[nodiscard]] Result verify(const Reference& ref) const;

private:
    // Fills verdict/note after a candidate paper was found.
    void judge(const Reference& ref, PaperId id, double title_score, Result& out) const;

    const PaperDb& db_;   // reference member: Verifier never owns the database
    VerifierConfig cfg_;
};

}  // namespace citeverify

#include "citeverify/core/verifier.hpp"

#include "citeverify/core/text.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>

namespace citeverify {

Verifier::Verifier(const PaperDb& db, VerifierConfig cfg) : db_(db), cfg_(cfg) {}

void Verifier::judge(const Reference& ref, PaperId id, double title_score, Result& out) const {
    const Paper& p = db_.paper(id);
    out.paper = id;
    out.score = title_score;

    std::string problems;
    auto add = [&problems](const std::string& s) {   // lambda capturing by reference
        if (!problems.empty()) problems += "; ";
        problems += s;
    };

    if (!ref.title.empty() && title_score < cfg_.verified_threshold)
        add("title differs (similarity " + std::to_string(title_score).substr(0, 4) + ")");
    if (ref.year && p.year != 0 && std::abs(*ref.year - p.year) > cfg_.year_tolerance)
        add("year differs: " + std::to_string(*ref.year) + " vs " + std::to_string(p.year));
    if (!ref.doi.empty() && !p.doi.empty() && text::normalize_doi(p.doi) != ref.doi)
        add("DOI differs");
    if (!ref.authors.empty() && !p.authors.empty()) {
        const std::string first = text::last_name(ref.authors.front());
        // std::ranges::any_of: C++20 algorithm over a whole range + a predicate.
        const bool found = std::ranges::any_of(p.authors, [&](const std::string& a) {
            return text::last_name(a) == first;
        });
        if (!found) add("first author '" + first + "' not among authors");
    }

    out.verdict = problems.empty() ? Verdict::Verified : Verdict::FoundDiffers;
    out.note = problems.empty() ? "ok" : problems;
}

Result Verifier::verify(const Reference& ref) const {
    const auto t0 = std::chrono::steady_clock::now();
    // Designated initializers (C++20): name the fields you set, the rest get
    // their defaults. Clearer than positional {ref.index, ...}.
    Result out{.index = ref.index};

    const std::string title_norm = text::normalize(ref.title);

    // ---- Rung 1 (Week 2): shared cache lookup goes here. ------------------

    // ---- Rung 2a: exact DOI lookup, O(1). --------------------------------
    bool decided = false;
    if (!ref.doi.empty()) {
        if (auto id = db_.find_by_doi(ref.doi)) {           // std::optional converts to bool
            const double s = title_norm.empty()
                ? 1.0
                : text::jaccard(text::trigrams(title_norm), db_.title_trigrams(*id));
            ++out.comparisons;
            out.method = MatchMethod::Doi;
            judge(ref, *id, s, out);
            decided = true;
        }
    }

    // ---- Rung 2b: exact normalised-title lookup, O(1). -------------------
    if (!decided && !title_norm.empty()) {
        if (auto id = db_.find_by_title(title_norm)) {
            out.method = MatchMethod::ExactTitle;
            judge(ref, *id, 1.0, out);
            decided = true;
        }
    }

    // ---- Rung 3: fuzzy search. Week 1 = brute force over ALL papers. ------
    // This is deliberately the expensive path: a fake reference always ends
    // here and scans the whole database. Week 3 replaces the loop with a
    // trigram-index candidate lookup (millions -> hundreds of comparisons).
    if (!decided && !title_norm.empty()) {
        const auto q = text::trigrams(title_norm);
        double best = 0.0;
        PaperId best_id = 0;
        for (PaperId id = 0; id < db_.size(); ++id) {
            const double s = text::jaccard(q, db_.title_trigrams(id));
            if (s > best) { best = s; best_id = id; }
        }
        out.comparisons += db_.size();
        out.method = MatchMethod::Fuzzy;
        if (best >= cfg_.candidate_threshold) {
            judge(ref, best_id, best, out);
        } else {
            out.verdict = Verdict::NotFound;
            out.score = best;
            out.note = "not found in our sources (best similarity " +
                       std::to_string(best).substr(0, 4) + ")";
        }
        decided = true;
    }

    if (!decided) {
        out.verdict = Verdict::NotFound;
        out.note = ref.doi.empty() ? "no title or DOI could be parsed" : "DOI not in our sources";
    }

    out.micros = std::chrono::duration<double, std::micro>(
                     std::chrono::steady_clock::now() - t0).count();
    return out;
}

}  // namespace citeverify

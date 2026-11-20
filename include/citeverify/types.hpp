// types.hpp — THE shared interface between Pair A (engine) and Pair B (verifier).
//
// Rule: changing anything in this file needs a review from both pairs.
// Everything else in the project can evolve independently.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace citeverify {

// Index of a paper inside PaperDb (0 .. size-1). 32 bits is enough for
// ~4 billion papers and keeps index posting lists small (memory bandwidth!).
using PaperId = std::uint32_t;

// One real paper from our sources (arXiv snapshot, dblp, ...).
struct Paper {
    std::string id;                    // arXiv id or dblp key
    std::string doi;                   // may be empty
    std::string title;
    std::vector<std::string> authors;  // "First Last" strings
    int year = 0;                      // 0 = unknown
};

// Ground-truth label, only present in evaluation batches.
enum class Label { Unknown, Real, Mutated, Fake };

// One reference as extracted from an LLM answer.
struct Reference {
    std::size_t index = 0;             // position in the input (for the reorder buffer)
    std::string raw;                   // the original line
    std::string title;
    std::string doi;                   // empty if none was written
    std::vector<std::string> authors;
    std::optional<int> year;           // std::optional = "maybe a value", no magic -1
    Label label = Label::Unknown;
};

enum class Verdict { Verified, FoundDiffers, NotFound };

// Which rung of the verification ladder produced the answer.
enum class MatchMethod { None, Cache, Doi, ExactTitle, Fuzzy };

// Output of verifying one reference.
struct Result {
    std::size_t index = 0;             // same as Reference::index
    Verdict verdict = Verdict::NotFound;
    MatchMethod method = MatchMethod::None;
    std::optional<PaperId> paper;      // best match, if any
    double score = 0.0;                // title similarity of best match (0..1)
    std::string note;                  // human-readable reason ("year differs: 2019 vs 2017")
    std::uint64_t comparisons = 0;     // similarity computations done = work done
    double micros = 0.0;               // wall time spent on this reference
};

// `constexpr` functions can run at compile time; here they are just cheap
// helpers that live in the header without violating the one-definition rule.
constexpr std::string_view to_string(Verdict v) {
    switch (v) {
        case Verdict::Verified:     return "VERIFIED";
        case Verdict::FoundDiffers: return "FOUND_DIFFERS";
        case Verdict::NotFound:     return "NOT_FOUND";
    }
    return "?";
}

constexpr std::string_view to_string(MatchMethod m) {
    switch (m) {
        case MatchMethod::None:       return "none";
        case MatchMethod::Cache:      return "cache";
        case MatchMethod::Doi:        return "doi";
        case MatchMethod::ExactTitle: return "exact_title";
        case MatchMethod::Fuzzy:      return "fuzzy";
    }
    return "?";
}

constexpr std::string_view to_string(Label l) {
    switch (l) {
        case Label::Unknown: return "unknown";
        case Label::Real:    return "real";
        case Label::Mutated: return "mutated";
        case Label::Fake:    return "fake";
    }
    return "?";
}

// The verdict we *expect* for a labelled reference (used for accuracy).
constexpr std::optional<Verdict> expected_verdict(Label l) {
    switch (l) {
        case Label::Real:    return Verdict::Verified;
        case Label::Mutated: return Verdict::FoundDiffers;
        case Label::Fake:    return Verdict::NotFound;
        case Label::Unknown: return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace citeverify

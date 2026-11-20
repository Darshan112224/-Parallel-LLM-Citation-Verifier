// test_core.cpp — tiny self-contained tests (no framework, just CHECK).
// Run: ctest --test-dir build   or   ./build/test_core
#include "citeverify/core/paper_db.hpp"
#include "citeverify/core/reference_parser.hpp"
#include "citeverify/core/text.hpp"
#include "citeverify/core/verifier.hpp"

#include <cmath>
#include <iostream>
#include <source_location>

using namespace citeverify;

static int failures = 0;

// std::source_location (C++20) replaces the old __FILE__/__LINE__ macros:
// it is captured automatically at the *call site* via the default argument.
static void check(bool ok, const char* what,
                  std::source_location loc = std::source_location::current()) {
    if (!ok) {
        ++failures;
        std::cerr << loc.file_name() << ':' << loc.line() << ": FAILED: " << what << '\n';
    }
}
#define CHECK(expr) check((expr), #expr)

static void test_text() {
    CHECK(text::normalize("  Attention Is ALL you need! ") == "attention is all you need");
    CHECK(text::normalize("BERT: Pre-training") == "bert pre training");
    CHECK(text::normalize_doi("https://doi.org/10.1000/ABC.") == "10.1000/abc");
    CHECK(text::normalize_doi("doi:10.1000/x") == "10.1000/x");
    CHECK(text::last_name("Ashish Vaswani") == "vaswani");
    CHECK(text::last_name("Vaswani, A.") == "vaswani");

    const auto a = text::trigrams("abc");
    CHECK(a.size() == 3);                      // " ab", "abc", "bc "
    CHECK(std::abs(text::jaccard(a, a) - 1.0) < 1e-12);
    CHECK(text::jaccard(a, text::trigrams("xyz")) == 0.0);
    const double s = text::jaccard(text::trigrams("attention is all you need"),
                                   text::trigrams("attention is all we need"));
    CHECK(s > 0.6 && s < 1.0);
}

static void test_parser() {
    const auto r = parse_reference_line(
        R"(A. Vaswani, N. Shazeer et al. "Attention Is All You Need." NeurIPS 2017. doi:10.48550/arXiv.1706.03762  # label=real)", 7);
    CHECK(r.index == 7);
    CHECK(r.title == "Attention Is All You Need");
    CHECK(r.doi == "10.48550/arxiv.1706.03762");
    CHECK(r.year.has_value() && *r.year == 2017);
    CHECK(r.label == Label::Real);
    CHECK(!r.authors.empty() && r.authors.front() == "A. Vaswani");

    const auto n = parse_reference_line("Smith J. Quantum Widgets for Everyone. Journal of Stuff 2021.", 0);
    CHECK(n.title == "Quantum Widgets for Everyone");
    CHECK(n.year == 2021);   // optional<int> == int compares the value (false if empty)
}

static void test_verifier() {
    std::vector<Paper> papers = {
        {.id = "p0", .doi = "10.5555/a", .title = "Attention Is All You Need",
         .authors = {"Ashish Vaswani", "Noam Shazeer"}, .year = 2017},
        {.id = "p1", .doi = "", .title = "Deep Residual Learning for Image Recognition",
         .authors = {"Kaiming He"}, .year = 2015},
    };
    PaperDb db = PaperDb::from_papers(std::move(papers));
    db.build_title_trigrams();
    const Verifier v(db);

    auto verdict_of = [&](const char* line) { return v.verify(parse_reference_line(line, 0)); };

    auto r1 = verdict_of(R"(A. Vaswani. "Attention Is All You Need". 2017. doi:10.5555/a)");
    CHECK(r1.verdict == Verdict::Verified && r1.method == MatchMethod::Doi);

    auto r2 = verdict_of(R"(K. He. "Deep Residual Learning for Image Recognition". CVPR 2016.)");
    CHECK(r2.verdict == Verdict::Verified && r2.method == MatchMethod::ExactTitle);  // 2016 within ±1

    auto r3 = verdict_of(R"(K. He. "Deep Residual Learning for Image Recognition". 2009.)");
    CHECK(r3.verdict == Verdict::FoundDiffers);                                      // wrong year

    auto r4 = verdict_of(R"(K. He. "Deep Residual Learning for Image Classification". 2015.)");
    CHECK(r4.verdict == Verdict::FoundDiffers && r4.method == MatchMethod::Fuzzy);   // mutated title

    auto r5 = verdict_of(R"(J. Doe. "Quantum Gravity Explains Large Language Model Hallucinations". 2024.)");
    CHECK(r5.verdict == Verdict::NotFound);
    CHECK(r5.comparisons == db.size());                                              // full scan = expensive
}

int main() {
    test_text();
    test_parser();
    test_verifier();
    if (failures == 0) std::cout << "all tests passed\n";
    return failures == 0 ? 0 : 1;
}

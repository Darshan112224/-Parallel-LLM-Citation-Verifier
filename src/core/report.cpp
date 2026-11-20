#include "citeverify/core/report.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <map>
#include <numeric>
#include <string>

namespace citeverify {
namespace {

// Replace tabs/newlines so a field can never break the TSV format.
std::string tsv_safe(std::string s) {
    std::ranges::replace(s, '\t', ' ');
    std::ranges::replace(s, '\n', ' ');
    return s;
}

}  // namespace

void write_report(std::ostream& out, std::span<const Reference> refs,
                  std::span<const Result> results, const PaperDb& db) {
    out << "index\tverdict\tmethod\tscore\tmatched_id\tmatched_title\tnote\tcomparisons\tmicros\tlabel\treference\n";
    for (const Result& r : results) {
        const Reference& ref = refs[r.index];
        out << r.index << '\t' << to_string(r.verdict) << '\t' << to_string(r.method) << '\t'
            << std::fixed << std::setprecision(3) << r.score << '\t';
        if (r.paper) out << db.paper(*r.paper).id << '\t' << tsv_safe(db.paper(*r.paper).title);
        else         out << "-\t-";
        out << '\t' << tsv_safe(r.note) << '\t' << r.comparisons << '\t'
            << std::setprecision(1) << r.micros << '\t' << to_string(ref.label) << '\t'
            << tsv_safe(ref.raw) << '\n';
    }
}

void print_summary(std::ostream& out, std::span<const Reference> refs,
                   std::span<const Result> results) {
    std::map<std::string_view, int> by_verdict;
    struct Cost { int n = 0; double micros = 0; std::uint64_t cmp = 0; };
    std::map<std::string_view, Cost> by_method;
    int labelled = 0, correct = 0;
    std::map<std::pair<std::string_view, std::string_view>, int> confusion;  // (label, verdict)

    for (const Result& r : results) {
        ++by_verdict[to_string(r.verdict)];
        auto& c = by_method[to_string(r.method)];
        ++c.n; c.micros += r.micros; c.cmp += r.comparisons;
        const Reference& ref = refs[r.index];
        if (auto want = expected_verdict(ref.label)) {
            ++labelled;
            if (*want == r.verdict) ++correct;
            ++confusion[{to_string(ref.label), to_string(r.verdict)}];
        }
    }

    out << "\n=== Summary: " << results.size() << " references ===\n";
    // Structured bindings (C++17): unpack a pair into named variables.
    for (const auto& [verdict, n] : by_verdict) out << "  " << std::left << std::setw(14) << verdict << n << '\n';

    out << "\n--- Cost per ladder rung (this is the load imbalance) ---\n";
    out << "  method        count   avg_us      total_ms   avg_comparisons\n";
    for (const auto& [m, c] : by_method) {
        out << "  " << std::left << std::setw(13) << m << std::right << std::setw(6) << c.n
            << std::fixed << std::setprecision(1) << std::setw(10) << c.micros / c.n
            << std::setw(13) << c.micros / 1000.0
            << std::setw(17) << (c.cmp / std::max(1, c.n)) << '\n';
    }

    if (labelled > 0) {
        out << "\n--- Accuracy on labelled references ---\n";
        out << "  " << correct << " / " << labelled << " correct ("
            << std::setprecision(1) << 100.0 * correct / labelled << "%)\n";
        out << "  label -> verdict counts:\n";
        for (const auto& [key, n] : confusion)
            out << "    " << std::left << std::setw(8) << key.first << " -> "
                << std::setw(14) << key.second << n << '\n';
    }
}

void print_imbalance_analysis(std::ostream& out, std::span<const Result> results,
                              std::span<const int> thread_counts) {
    if (results.empty()) return;
    std::vector<double> t;
    t.reserve(results.size());
    for (const auto& r : results) t.push_back(r.micros);
    const double total = std::accumulate(t.begin(), t.end(), 0.0);

    // How concentrated is the work? Share of time spent in the top 10% references.
    auto sorted = t;
    std::ranges::sort(sorted, std::greater<>{});
    const std::size_t top = std::max<std::size_t>(1, sorted.size() / 10);
    const double top_time = std::accumulate(sorted.begin(), sorted.begin() + static_cast<long>(top), 0.0);

    out << "\n--- Load imbalance preview (simulated from measured per-reference times) ---\n";
    out << "  top 10% most expensive references take " << std::fixed << std::setprecision(1)
        << 100.0 * top_time / total << "% of verification time\n";
    out << "  threads  ideal_ms  static_chunks_ms  imbalance(static/ideal)\n";
    for (int p : thread_counts) {
        // Static scheduling: thread k gets references [k*n/p, (k+1)*n/p).
        const std::size_t n = t.size();
        double worst = 0;
        for (int k = 0; k < p; ++k) {
            const std::size_t b = n * static_cast<std::size_t>(k) / static_cast<std::size_t>(p);
            const std::size_t e = n * static_cast<std::size_t>(k + 1) / static_cast<std::size_t>(p);
            worst = std::max(worst, std::accumulate(t.begin() + static_cast<long>(b),
                                                    t.begin() + static_cast<long>(e), 0.0));
        }
        const double ideal = total / p;
        out << "  " << std::setw(7) << p << std::setw(10) << std::setprecision(2) << ideal / 1000.0
            << std::setw(18) << worst / 1000.0 << std::setw(25) << std::setprecision(2)
            << worst / ideal << '\n';
    }
    out << "  (imbalance > 1.0 = time lost to idle threads; dynamic scheduling in Week 3 attacks this)\n";
}

}  // namespace citeverify

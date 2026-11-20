// main.cpp — Week 1 sequential baseline.
//
//   citeverify --db data/papers_sample.tsv --refs data/refs_busy.txt [--out report.tsv]
//
// Stages (each timed): load DB -> build trigrams -> parse refs -> verify all -> write report.
// The parallel version keeps exactly this structure; only "verify all" and
// "build trigrams" get threads, so T(1) measured here is the baseline for
// every speedup number S(p) = T(1) / T(p) in the final report.
#include "citeverify/core/paper_db.hpp"
#include "citeverify/core/reference_parser.hpp"
#include "citeverify/core/report.hpp"
#include "citeverify/core/verifier.hpp"
#include "citeverify/util/timer.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace citeverify;

namespace {

struct Options {
    std::string db_path;
    std::string refs_path;
    std::string out_path = "report.tsv";
};

void usage() {
    std::cerr << "usage: citeverify --db <papers.tsv> --refs <refs.txt> [--out report.tsv]\n";
}

bool parse_args(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--db") o.db_path = next();
        else if (a == "--refs") o.refs_path = next();
        else if (a == "--out") o.out_path = next();
        else if (a == "-h" || a == "--help") return false;
        else { std::cerr << "unknown option: " << a << '\n'; return false; }
    }
    return !o.db_path.empty() && !o.refs_path.empty();
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) { usage(); return 2; }

    // Stage times in ms; used at the end to compute the sequential fraction f.
    double t_load = 0, t_index = 0, t_parse = 0, t_verify = 0, t_write = 0, t_total = 0;

    try {
        ScopedTimer total("TOTAL", &t_total);

        PaperDb db = [&] {   // immediately-invoked lambda: lets `db` be initialised inside a timed scope
            ScopedTimer t("load database      [sequential]", &t_load);
            return PaperDb::load_tsv(opt.db_path);
        }();
        std::cerr << "[info] papers loaded: " << db.size() << '\n';

        {
            ScopedTimer t("build trigrams     [parallel in W3]", &t_index);
            db.build_title_trigrams();
        }

        std::vector<Reference> refs;
        {
            ScopedTimer t("parse references   [sequential]", &t_parse);
            std::ifstream in(opt.refs_path);
            if (!in) throw std::runtime_error("cannot open references: " + opt.refs_path);
            refs = parse_references(in);
        }
        std::cerr << "[info] references parsed: " << refs.size() << '\n';

        std::vector<Result> results;
        results.reserve(refs.size());
        {
            ScopedTimer t("verify all         [parallel in W2+]", &t_verify);
            const Verifier verifier(db);
            for (const Reference& r : refs) results.push_back(verifier.verify(r));
        }

        {
            ScopedTimer t("write report       [sequential]", &t_write);
            std::ofstream out(opt.out_path);
            if (!out) throw std::runtime_error("cannot write report: " + opt.out_path);
            write_report(out, refs, results, db);
        }

        print_summary(std::cout, refs, results);
        constexpr std::array threads{2, 4, 8};   // CTAD: std::array<int,3> deduced (C++17)
        print_imbalance_analysis(std::cout, results, threads);

        // Amdahl preview: parallelisable = trigram build + verification.
        const double seq = t_load + t_parse + t_write;
        const double par = t_index + t_verify;
        const double f = seq / (seq + par);
        std::cout << "\n--- Amdahl preview ---\n"
                  << "  sequential fraction f = " << f << '\n';
        for (int p : threads)
            std::cout << "  max speedup on " << p << " threads: " << 1.0 / (f + (1.0 - f) / p) << '\n';
        std::cout << "\nreport written to " << opt.out_path << '\n';
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

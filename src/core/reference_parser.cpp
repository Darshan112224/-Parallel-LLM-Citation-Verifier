#include "citeverify/core/reference_parser.hpp"

#include "citeverify/core/text.hpp"

#include <regex>
#include <string>

namespace citeverify {
namespace {

std::string_view trim(std::string_view s) {
    const auto ws = " \t\r\n";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

// `static const` regexes are compiled once (thread-safe since C++11: "magic
// statics"). std::regex is slow, but parsing is <1% of run time; if profiling
// says otherwise we replace it with a hand-written scanner.
const std::regex& doi_regex() {
    static const std::regex re(R"(10\.\d{4,9}/[^\s"<>]+)", std::regex::icase);
    return re;   // R"( ... )" = raw string literal: backslashes are not escapes
}
const std::regex& year_regex() {
    static const std::regex re(R"(\b(19|20)\d{2}\b)");
    return re;
}

Label parse_label(std::string_view v) {
    if (v == "real") return Label::Real;
    if (v == "mutated") return Label::Mutated;
    if (v == "fake") return Label::Fake;
    return Label::Unknown;
}

// Split an author list: "A. Vaswani, N. Shazeer and I. Polosukhin et al."
std::vector<std::string> split_authors(std::string_view s) {
    std::string t(s);
    for (const char* junk : {" et al.", " et al", " and ", " & "}) {
        for (auto pos = t.find(junk); pos != std::string::npos; pos = t.find(junk))
            t.replace(pos, std::string_view(junk).size(), ",");
    }
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= t.size()) {
        auto pos = t.find(',', start);
        if (pos == std::string::npos) pos = t.size();
        auto a = trim(std::string_view(t).substr(start, pos - start));
        // Skip initials-only fragments like "A." that come from "Vaswani, A."
        if (a.size() > 2) out.emplace_back(a);
        start = pos + 1;
    }
    return out;
}

}  // namespace

Reference parse_reference_line(std::string_view line, std::size_t index) {
    Reference ref;
    ref.index = index;

    // 1) Evaluation label "# label=xxx" at the end.
    if (auto pos = line.find("# label="); pos != std::string_view::npos) {
        ref.label = parse_label(trim(line.substr(pos + 8)));
        line = line.substr(0, pos);
    }
    line = trim(line);
    ref.raw = std::string(line);
    std::string rest = ref.raw;   // we cut found pieces out of `rest`

    // 2) DOI.
    std::smatch m;
    if (std::regex_search(rest, m, doi_regex())) {
        ref.doi = text::normalize_doi(m.str());
        rest.erase(static_cast<std::size_t>(m.position()), static_cast<std::size_t>(m.length()));
    }

    // 3) Title: prefer text in double quotes (ASCII "…" or UTF-8 “…”).
    std::size_t title_start = std::string::npos;
    std::size_t q1 = rest.find('"');
    std::size_t q2 = (q1 == std::string::npos) ? q1 : rest.find('"', q1 + 1);
    std::size_t qlen = 1;
    if (q2 == std::string::npos) {                      // try curly quotes (3 bytes each in UTF-8)
        q1 = rest.find("\xE2\x80\x9C");
        q2 = (q1 == std::string::npos) ? q1 : rest.find("\xE2\x80\x9D", q1 + 3);
        qlen = 3;
    }
    if (q1 != std::string::npos && q2 != std::string::npos) {
        ref.title = std::string(trim(std::string_view(rest).substr(q1 + qlen, q2 - q1 - qlen)));
        title_start = q1;
        // Trailing period inside the quotes: "Title." -> Title
        while (!ref.title.empty() && (ref.title.back() == '.' || ref.title.back() == ','))
            ref.title.pop_back();
    }

    // 4) Year: last 4-digit year outside the title.
    const std::string outside = (title_start == std::string::npos)
                                    ? rest
                                    : rest.substr(0, title_start) + " " + rest.substr(q2 + qlen);
    for (auto it = std::sregex_iterator(outside.begin(), outside.end(), year_regex());
         it != std::sregex_iterator(); ++it) {
        ref.year = std::stoi(it->str());
    }

    // 5) Authors: everything before the title.
    if (title_start != std::string::npos) {
        ref.authors = split_authors(trim(std::string_view(rest).substr(0, title_start)));
    } else {
        // No quotes: assume "Authors. Title. Venue Year." and take the longest
        // sentence as the title — a heuristic, good enough for Week 1.
        std::string_view r = rest;
        std::string_view best;
        std::size_t start = 0;
        while (start < r.size()) {
            auto pos = r.find(". ", start);
            if (pos == std::string_view::npos) pos = r.size();
            auto piece = trim(r.substr(start, pos - start));
            if (piece.size() > best.size()) best = piece;
            start = pos + 2;
        }
        ref.title = std::string(best);
    }
    return ref;
}

std::vector<Reference> parse_references(std::istream& in) {
    std::vector<Reference> refs;
    std::string line;
    while (std::getline(in, line)) {
        const auto t = trim(line);
        if (t.empty() || t.front() == '#') continue;
        refs.push_back(parse_reference_line(t, refs.size()));
    }
    return refs;
}

}  // namespace citeverify

#include "citeverify/core/paper_db.hpp"

#include "citeverify/core/text.hpp"

#include <charconv>
#include <fstream>
#include <stdexcept>

namespace citeverify {
namespace {  // anonymous namespace = helpers visible only in this .cpp file

// Split `line` on `sep` into views (no copies). The views point into `line`,
// so they are only valid while `line` lives.
std::vector<std::string_view> split(std::string_view line, char sep) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const auto pos = line.find(sep, start);
        parts.push_back(line.substr(start, pos - start));
        if (pos == std::string_view::npos) break;
        start = pos + 1;
    }
    return parts;
}

int parse_year(std::string_view s) {
    int y = 0;
    // std::from_chars (C++17): fastest standard number parser — no locale,
    // no exceptions, no allocation. Good for parsing millions of lines.
    std::from_chars(s.data(), s.data() + s.size(), y);
    return y;
}

}  // namespace

PaperDb PaperDb::load_tsv(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open paper database: " + path.string());

    std::vector<Paper> papers;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.front() == '#') continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();  // Windows line endings
        const auto f = split(line, '\t');
        if (f.size() < 3) continue;  // malformed line: skip, don't crash

        Paper p;
        p.id = std::string(f[0]);
        p.doi = std::string(f[1]);
        p.title = std::string(f[2]);
        if (f.size() > 3 && !f[3].empty())
            for (auto a : split(f[3], ';'))
                if (!a.empty()) p.authors.emplace_back(a);
        if (f.size() > 4) p.year = parse_year(f[4]);
        papers.push_back(std::move(p));  // std::move: steal the strings, no copy
    }
    return from_papers(std::move(papers));
}

PaperDb PaperDb::from_papers(std::vector<Paper> papers) {
    PaperDb db;
    db.papers_ = std::move(papers);
    db.build_maps();
    return db;
}

void PaperDb::build_maps() {
    by_doi_.reserve(papers_.size());
    by_title_.reserve(papers_.size());
    for (PaperId id = 0; id < papers_.size(); ++id) {
        const Paper& p = papers_[id];
        if (!p.doi.empty()) by_doi_.try_emplace(text::normalize_doi(p.doi), id);
        // try_emplace keeps the FIRST paper for duplicate titles.
        by_title_.try_emplace(text::normalize(p.title), id);
    }
}

void PaperDb::build_title_trigrams() {
    // Week 1: sequential. Week 3: each thread fills its own slice of trigrams_
    // (data decomposition, no locking needed because slices don't overlap).
    trigrams_.resize(papers_.size());
    for (PaperId id = 0; id < papers_.size(); ++id)
        trigrams_[id] = text::trigrams(text::normalize(papers_[id].title));
}

std::optional<PaperId> PaperDb::find_by_doi(std::string_view doi_norm) const {
    // Heterogeneous lookup: find() takes the string_view directly (see header).
    if (auto it = by_doi_.find(doi_norm); it != by_doi_.end()) return it->second;
    return std::nullopt;
}

std::optional<PaperId> PaperDb::find_by_title(std::string_view title_norm) const {
    if (auto it = by_title_.find(title_norm); it != by_title_.end()) return it->second;
    return std::nullopt;
}

}  // namespace citeverify

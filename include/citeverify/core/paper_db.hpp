// paper_db.hpp — the in-memory database of real papers (Pair B).
//
// Week 1: load a TSV file, build exact-lookup hash maps and one trigram set
// per title (used by the brute-force fuzzy search).
// Week 3: the brute-force scan is replaced by index::TrigramIndex.
#pragma once

#include "citeverify/types.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace citeverify {

// --- C++20 heterogeneous lookup -------------------------------------------
// Normally map.find(key) needs a std::string, so looking up a string_view
// forces a temporary allocation. If the hash and the equality both declare
// `is_transparent`, C++20 unordered containers accept *any* type that can be
// hashed/compared — here std::string_view — with zero copies.
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};
template <class V>
using StringMap = std::unordered_map<std::string, V, StringHash, std::equal_to<>>;

class PaperDb {
public:
    // TSV columns: id <TAB> doi <TAB> title <TAB> authors(';'-separated) <TAB> year
    // Lines starting with '#' are comments. Throws std::runtime_error on I/O error.
    static PaperDb load_tsv(const std::filesystem::path& path);

    // Build from papers already in memory (used by tests and generators).
    static PaperDb from_papers(std::vector<Paper> papers);

    // Precompute normalised titles + trigram sets. Separate from loading so the
    // two stages can be timed separately (loading = sequential, this = will be
    // parallel in Week 3).
    void build_title_trigrams();

    [[nodiscard]] std::size_t size() const noexcept { return papers_.size(); }
    [[nodiscard]] const Paper& paper(PaperId id) const { return papers_.at(id); }

    // Arguments must already be normalised (text::normalize_doi / normalize).
    [[nodiscard]] std::optional<PaperId> find_by_doi(std::string_view doi_norm) const;
    [[nodiscard]] std::optional<PaperId> find_by_title(std::string_view title_norm) const;

    [[nodiscard]] std::span<const std::uint32_t> title_trigrams(PaperId id) const {
        return trigrams_[id];
    }
    [[nodiscard]] bool has_trigrams() const noexcept { return trigrams_.size() == papers_.size(); }

private:
    void build_maps();

    std::vector<Paper> papers_;
    std::vector<std::vector<std::uint32_t>> trigrams_;  // trigrams_[id]
    StringMap<PaperId> by_doi_;
    StringMap<PaperId> by_title_;
};

}  // namespace citeverify

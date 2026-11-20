#include "citeverify/core/text.hpp"

#include <algorithm>
#include <cctype>

namespace citeverify::text {

std::string normalize(std::string_view s) {
    std::string out;
    out.reserve(s.size());          // one allocation instead of many
    bool pending_space = false;
    for (unsigned char c : s) {     // unsigned: std::isalnum on negative chars is UB
        if (std::isalnum(c)) {
            if (pending_space && !out.empty()) out.push_back(' ');
            pending_space = false;
            out.push_back(static_cast<char>(std::tolower(c)));
        } else {
            pending_space = true;   // any punctuation / whitespace / UTF-8 byte
        }
    }
    return out;
}

std::string normalize_doi(std::string_view s) {
    // Strip common prefixes. C++20 added starts_with/ends_with to string_view.
    auto lower = std::string(s);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string_view v = lower;
    for (std::string_view prefix : {"https://doi.org/", "http://doi.org/",
                                    "https://dx.doi.org/", "doi.org/", "doi:"}) {
        if (v.starts_with(prefix)) { v.remove_prefix(prefix.size()); break; }
    }
    while (!v.empty() && (v.front() == ' ')) v.remove_prefix(1);
    // Trailing punctuation often sticks to DOIs at the end of a sentence.
    while (!v.empty() && std::string_view(".,;:)]}\"' ").find(v.back()) != std::string_view::npos)
        v.remove_suffix(1);
    return std::string(v);
}

std::string last_name(std::string_view author) {
    // "Vaswani, A." -> the part before the comma is the last name.
    if (auto comma = author.find(','); comma != std::string_view::npos) {
        // ^ C++17 "if with initializer": `comma` only exists inside this if.
        return normalize(author.substr(0, comma));
    }
    const std::string n = normalize(author);
    const auto sp = n.rfind(' ');
    return sp == std::string::npos ? n : n.substr(sp + 1);
}

std::vector<std::uint32_t> trigrams(std::string_view normalized) {
    std::vector<std::uint32_t> out;
    if (normalized.empty()) return out;
    // Pad so that word starts/ends also produce trigrams.
    std::string p;
    p.reserve(normalized.size() + 2);
    p.push_back(' ');
    p.append(normalized);
    p.push_back(' ');
    out.reserve(p.size());
    for (std::size_t i = 0; i + 2 < p.size(); ++i) {
        const auto a = static_cast<unsigned char>(p[i]);
        const auto b = static_cast<unsigned char>(p[i + 1]);
        const auto c = static_cast<unsigned char>(p[i + 2]);
        out.push_back((std::uint32_t{a} << 16) | (std::uint32_t{b} << 8) | c);
    }
    // C++20 ranges: std::ranges::sort takes the whole container, no begin/end pair.
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

double jaccard(std::span<const std::uint32_t> a, std::span<const std::uint32_t> b) {
    if (a.empty() && b.empty()) return 1.0;
    std::size_t i = 0, j = 0, inter = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j])      { ++inter; ++i; ++j; }
        else if (a[i] < b[j])  { ++i; }
        else                   { ++j; }
    }
    const std::size_t uni = a.size() + b.size() - inter;
    return static_cast<double>(inter) / static_cast<double>(uni);
}

}  // namespace citeverify::text

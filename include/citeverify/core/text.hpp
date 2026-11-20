// text.hpp — string normalisation and similarity (Pair B).
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace citeverify::text {

// "Attention Is All You Need!" -> "attention is all you need"
// Lower-case ASCII, every non-alphanumeric char becomes a space, runs of
// spaces collapse to one, leading/trailing spaces removed.
//
// std::string_view = a non-owning (pointer, length) view of characters. It
// accepts std::string, string literals and substrings without copying.
[[nodiscard]] std::string normalize(std::string_view s);

// "https://doi.org/10.1000/ABC." -> "10.1000/abc"
[[nodiscard]] std::string normalize_doi(std::string_view s);

// Last name of an author, normalised: "Ashish Vaswani" -> "vaswani",
// "Vaswani, A." -> "vaswani".
[[nodiscard]] std::string last_name(std::string_view author);

// Character trigrams of a *normalised* string, padded with spaces, packed
// into one uint32 each (3 bytes -> exact, no hash collisions), sorted and
// unique.  "abc" -> { " ab", "abc", "bc " }.
[[nodiscard]] std::vector<std::uint32_t> trigrams(std::string_view normalized);

// Jaccard similarity |A ∩ B| / |A ∪ B| of two sorted, unique trigram sets.
// Linear-time merge.  std::span = non-owning view of a contiguous array
// (works with vector, array, C arrays) — the "string_view for arrays".
[[nodiscard]] double jaccard(std::span<const std::uint32_t> a,
                             std::span<const std::uint32_t> b);

}  // namespace citeverify::text

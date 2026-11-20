// reference_parser.hpp — turn raw reference lines into Reference structs (Pair B).
//
// Input: one reference per line, roughly as an LLM writes it, e.g.
//   A. Vaswani, N. Shazeer et al. "Attention Is All You Need". NeurIPS 2017. doi:10.48550/arXiv.1706.03762
// Optional evaluation label at the end of the line:   # label=real|mutated|fake
// Blank lines and lines starting with '#' are ignored.
#pragma once

#include "citeverify/types.hpp"

#include <istream>
#include <string_view>
#include <vector>

namespace citeverify {

[[nodiscard]] Reference parse_reference_line(std::string_view line, std::size_t index);

// Reads every reference; `index` is the 0-based order of the *kept* lines.
[[nodiscard]] std::vector<Reference> parse_references(std::istream& in);

}  // namespace citeverify

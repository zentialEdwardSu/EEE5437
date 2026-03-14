#pragma once

#include <string_view>
#include <utility>
#include <vector>

namespace dic::some_lib {

using Run = std::pair<char, int>;

std::vector<Run> run_length_encode(std::string_view text);

int total_symbol_count(const std::vector<Run>& runs);

}  // namespace dic::some_lib

#include "some_lib/some_lib.h"

namespace dic::some_lib {

std::vector<Run> run_length_encode(std::string_view text) {
    std::vector<Run> runs;
    if (text.empty()) {
        return runs;
    }

    char current = text.front();
    int count = 1;

    for (std::size_t index = 1; index < text.size(); ++index) {
        if (text[index] == current) {
            ++count;
            continue;
        }

        runs.emplace_back(current, count);
        current = text[index];
        count = 1;
    }

    runs.emplace_back(current, count);
    return runs;
}

int total_symbol_count(const std::vector<Run>& runs) {
    int total = 0;
    for (const auto& [symbol, count] : runs) {
        static_cast<void>(symbol);
        total += count;
    }
    return total;
}

}  // namespace dic::some_lib

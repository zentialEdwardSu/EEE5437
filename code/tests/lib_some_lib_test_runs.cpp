#include <iostream>

#include "some_lib/some_lib.h"

int main() {
    const auto runs = dic::some_lib::run_length_encode("AAABBCCCC");

    if (runs.size() != 3) {
        std::cerr << "expected 3 runs, got " << runs.size() << '\n';
        return 1;
    }

    if (runs[0].first != 'A' || runs[0].second != 3) {
        std::cerr << "unexpected first run\n";
        return 1;
    }

    if (dic::some_lib::total_symbol_count(runs) != 9) {
        std::cerr << "unexpected symbol count\n";
        return 1;
    }

    return 0;
}

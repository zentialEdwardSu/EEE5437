#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "hw1/hw1_text_prob_analyzer.h"

namespace {

bool expect_true(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool expect_near(double actual, double expected, double tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << message << '\n';
        std::cerr << "expected: " << expected << '\n';
        std::cerr << "actual:   " << actual << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    dic_hw1_text_analysis text_analysis;
    dic_hw1_text_analysis file_analysis;
    char* text_report = nullptr;
    char* file_report = nullptr;

    ok &= expect_true(
        dic_hw1_analyze_text(
            reinterpret_cast<const unsigned char*>("AAABCC"),
            6,
            &text_analysis
        ) == DIC_HW1_OK,
        "Text analysis should succeed."
    );
    ok &= expect_true(text_analysis.total_symbols == 6, "AAABCC should have 6 total symbols.");
    ok &= expect_true(text_analysis.unique_symbols == 3, "AAABCC should have 3 unique symbols.");
    ok &= expect_true(text_analysis.counts[static_cast<unsigned char>('A')] == 3, "A count mismatch.");
    ok &= expect_true(text_analysis.counts[static_cast<unsigned char>('B')] == 1, "B count mismatch.");
    ok &= expect_true(text_analysis.counts[static_cast<unsigned char>('C')] == 2, "C count mismatch.");
    ok &= expect_near(
        text_analysis.entropy,
        1.4591479170272448,
        1e-9,
        "Entropy for AAABCC should match the Shannon entropy."
    );

    text_report = dic_hw1_build_report(&text_analysis);
    ok &= expect_true(text_report != nullptr, "Report allocation for text analysis should succeed.");
    ok &= expect_true(
        text_report != nullptr &&
            std::string(text_report).find("entropy=1.459148 bits/symbol") != std::string::npos,
        "Summary should print the entropy with six decimal places."
    );
    ok &= expect_true(
        text_report != nullptr &&
            std::string(text_report).find("0x41 ('A'): count=3, probability=0.500000") != std::string::npos,
        "Summary should include the distribution for A."
    );

    const auto input_path = std::filesystem::current_path() / "hw1_test_input.bin";
    std::string file_content(128, 'A');
    file_content.push_back('\n');
    file_content.push_back('\0');
    file_content.push_back('\t');

    {
        std::ofstream output(input_path, std::ios::binary);
        if (!output) {
            std::cerr << "Failed to create temp test file.\n";
            return 1;
        }
        output.write(file_content.data(), static_cast<std::streamsize>(file_content.size()));
    }

    ok &= expect_true(
        dic_hw1_analyze_file(input_path.string().c_str(), &file_analysis) == DIC_HW1_OK,
        "File analysis should succeed."
    );
    ok &= expect_true(file_analysis.total_symbols == file_content.size(), "File symbol count mismatch.");
    ok &= expect_true(file_analysis.unique_symbols == 4, "File should contain 4 unique symbols.");
    ok &= expect_true(file_analysis.counts[static_cast<unsigned char>('A')] == 128, "File A count mismatch.");
    ok &= expect_true(file_analysis.counts[static_cast<unsigned char>('\n')] == 1, "Newline count mismatch.");
    ok &= expect_true(file_analysis.counts[static_cast<unsigned char>('\0')] == 1, "NUL count mismatch.");
    ok &= expect_true(file_analysis.counts[static_cast<unsigned char>('\t')] == 1, "Tab count mismatch.");

    file_report = dic_hw1_build_report(&file_analysis);
    ok &= expect_true(file_report != nullptr, "Report allocation for file analysis should succeed.");
    ok &= expect_true(
        file_report != nullptr &&
            std::string(file_report).find(R"(0x00 (\0): count=1, probability=0.007634)") != std::string::npos,
        "Summary should include the low-probability NUL symbol."
    );
    ok &= expect_true(
        file_report != nullptr &&
            std::string(file_report).find(R"(0x09 (\t): count=1, probability=0.007634)") != std::string::npos,
        "Summary should include the low-probability tab symbol."
    );
    ok &= expect_true(
        file_report != nullptr &&
            std::string(file_report).find(R"(0x0A (\n): count=1, probability=0.007634)") != std::string::npos,
        "Summary should include the low-probability newline symbol."
    );

    dic_hw1_free_report(text_report);
    dic_hw1_free_report(file_report);

    std::error_code remove_error;
    std::filesystem::remove(input_path, remove_error);

    return ok ? 0 : 1;
}

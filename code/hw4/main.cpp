#include <cstdlib>
#include <iostream>
#include <string>

#include "hw4/hw4_opencv_codec.hpp"
extern "C" {
#include "errors/errors.h"
}

namespace {

const char* kHw4UsageText =
    "usage:\n"
    "  hw4 compress <input-image> <output.dic53> <levels> <quality>\n"
    "  hw4 decompress <input.dic53> <output-image>\n";

bool parse_positive_int(const char* text, int* value) {
    char* end = nullptr;
    long parsed = std::strtol(text, &end, 10);
    if (text == nullptr || value == nullptr || end == text || *end != '\0' || parsed <= 0 || parsed > 1000000)
        return false;
    *value = static_cast<int>(parsed);
    return true;
}

void print_usage() {
    std::cerr << kHw4UsageText;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string error_message;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    const std::string mode = argv[1];
    if (mode == "compress") {
        int levels = 0;
        int quality = 0;
        std::string preview_path;

        if (argc != 6 || !parse_positive_int(argv[4], &levels) || !parse_positive_int(argv[5], &quality)) {
            print_usage();
            return 1;
        }

        if (!dic_hw4_compress_image_file(
                argv[2],
                argv[3],
                levels,
                quality,
                &preview_path,
                &error_message)) {
            std::cerr << "error: " << error_message << '\n';
            return 1;
        }

        std::cout << "wrote " << argv[3] << '\n';
        std::cout << "preview " << preview_path << '\n';
        return 0;
    }

    if (mode == "decompress") {
        if (argc != 4) {
            print_usage();
            return 1;
        }

        if (!dic_hw4_decompress_image_file(argv[2], argv[3], &error_message)) {
            std::cerr << "error: " << error_message << '\n';
            return 1;
        }

        std::cout << "wrote " << argv[3] << '\n';
        return 0;
    }

    print_usage();
    return 1;
}

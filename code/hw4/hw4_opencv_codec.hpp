#pragma once

#include <string>

bool dic_hw4_compress_image_file(
    const std::string& input_path,
    const std::string& output_path,
    int levels,
    int quality,
    std::string* preview_path,
    std::string* error_message
);

bool dic_hw4_decompress_image_file(
    const std::string& input_path,
    const std::string& output_path,
    std::string* error_message
);

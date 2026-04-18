#include "hw4/hw4_opencv_codec.hpp"

#include <filesystem>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

extern "C" {
#include "hw4/hw4_codec.h"
#include "hw4/hw4_dic53.h"
#include "hw4/hw4_preview.h"
#include "errors/errors.h"
}

namespace {

const char* kFailedToReadInputImage = "failed to read input image";
const char* kFailedToWritePreviewImage = "failed to write preview image";
const char* kFailedToWriteDecodedImage = "failed to write decoded image";

std::string build_preview_path(const std::string& output_path) {
    const std::filesystem::path output(output_path);
    return (output.parent_path() / (output.stem().string() + "_preview.png")).string();
}

bool is_supported_input(const cv::Mat& image) {
    return image.type() == CV_8UC1 || image.type() == CV_8UC3;
}

bool write_preview_image(
    const dic_hw4_encoded_image& encoded,
    const std::string& output_path,
    std::string* preview_path,
    std::string* error_message
) {
    dic_hw4_image_u8 preview{};
    const dic_status status = dic_hw4_build_preview_image(&encoded, &preview);
    if (status != DIC_STATUS_OK) {
        if (error_message != nullptr)
            *error_message = dic_status_message(status);
        return false;
    }

    const int type = preview.channels == 1 ? CV_8UC1 : CV_8UC3;
    const std::string derived_preview_path = build_preview_path(output_path);
    const cv::Mat preview_mat(preview.height, preview.width, type, preview.data);
    const bool ok = cv::imwrite(derived_preview_path, preview_mat);
    dic_hw4_image_free(&preview);
    if (!ok) {
        if (error_message != nullptr)
            *error_message = kFailedToWritePreviewImage;
        return false;
    }

    if (preview_path != nullptr)
        *preview_path = derived_preview_path;
    return true;
}

}  // namespace

bool dic_hw4_compress_image_file(
    const std::string& input_path,
    const std::string& output_path,
    int levels,
    int quality,
    std::string* preview_path,
    std::string* error_message
) {
    const cv::Mat loaded = cv::imread(input_path, cv::IMREAD_UNCHANGED);
    dic_hw4_encoded_image encoded{};

    if (loaded.empty()) {
        if (error_message != nullptr)
            *error_message = kFailedToReadInputImage;
        return false;
    }

    if (!is_supported_input(loaded)) {
        if (error_message != nullptr)
            *error_message = "only 8-bit 1-channel or 3-channel images are supported";
        return false;
    }

    const cv::Mat image = loaded.isContinuous() ? loaded : loaded.clone();
    const dic_status encode_status = dic_hw4_encode_image(
        image.data,
        image.cols,
        image.rows,
        image.channels(),
        levels,
        quality,
        &encoded
    );
    if (encode_status != DIC_STATUS_OK) {
        if (error_message != nullptr)
            *error_message = dic_status_message(encode_status);
        return false;
    }

    const dic_status write_status = dic_hw4_write_encoded_file(output_path.c_str(), &encoded);
    if (write_status != DIC_STATUS_OK) {
        if (error_message != nullptr)
            *error_message = dic_status_message(write_status);
        dic_hw4_encoded_free(&encoded);
        return false;
    }

    if (!write_preview_image(encoded, output_path, preview_path, error_message)) {
        dic_hw4_encoded_free(&encoded);
        return false;
    }

    dic_hw4_encoded_free(&encoded);
    return true;
}

bool dic_hw4_decompress_image_file(
    const std::string& input_path,
    const std::string& output_path,
    std::string* error_message
) {
    dic_hw4_encoded_image encoded{};
    dic_hw4_image_u8 decoded{};

    const dic_status read_status = dic_hw4_read_encoded_file(input_path.c_str(), &encoded);
    if (read_status != DIC_STATUS_OK) {
        if (error_message != nullptr)
            *error_message = dic_status_message(read_status);
        return false;
    }

    const dic_status decode_status = dic_hw4_decode_image(&encoded, &decoded);
    dic_hw4_encoded_free(&encoded);
    if (decode_status != DIC_STATUS_OK) {
        if (error_message != nullptr)
            *error_message = dic_status_message(decode_status);
        return false;
    }

    const int type = decoded.channels == 1 ? CV_8UC1 : CV_8UC3;
    const cv::Mat output(decoded.height, decoded.width, type, decoded.data);
    const bool ok = cv::imwrite(output_path, output);
    dic_hw4_image_free(&decoded);
    if (!ok) {
        if (error_message != nullptr)
            *error_message = kFailedToWriteDecodedImage;
        return false;
    }

    return true;
}

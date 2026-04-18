#include <filesystem>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "hw4/hw4_opencv_codec.hpp"
#include "test_helpers.h"

int main()
{
    const auto base = std::filesystem::current_path();
    const auto input_path = base / "hw4_test_input.png";
    const auto encoded_path = base / "hw4_test_output.dic53";
    const auto decoded_path = base / "hw4_test_output.png";
    const auto preview_path = base / "hw4_test_output_preview.png";
    cv::Mat image(8, 8, CV_8UC3);
    std::string error_message;

    for (int y = 0; y < image.rows; ++y)
    {
        for (int x = 0; x < image.cols; ++x)
        {
            image.at<cv::Vec3b>(y, x)[0] = static_cast<unsigned char>(10 + x * 5);
            image.at<cv::Vec3b>(y, x)[1] = static_cast<unsigned char>(20 + y * 7);
            image.at<cv::Vec3b>(y, x)[2] = static_cast<unsigned char>(30 + x * 3 + y * 2);
        }
    }

    DIC_EXPECT(cv::imwrite(input_path.string(), image));
    DIC_EXPECT(
        dic_hw4_compress_image_file(
            input_path.string(),
            encoded_path.string(),
            2,
            80,
            nullptr,
            &error_message
        )
    );
    DIC_EXPECT(std::filesystem::exists(encoded_path));
    DIC_EXPECT(std::filesystem::exists(preview_path));
    DIC_EXPECT(dic_hw4_decompress_image_file(encoded_path.string(), decoded_path.string(), &error_message));
    DIC_EXPECT(std::filesystem::exists(decoded_path));

    const cv::Mat decoded = cv::imread(decoded_path.string(), cv::IMREAD_UNCHANGED);
    DIC_EXPECT(!decoded.empty());
    DIC_EXPECT(decoded.type() == CV_8UC3);
    DIC_EXPECT(decoded.rows == image.rows);
    DIC_EXPECT(decoded.cols == image.cols);

    std::error_code ec;
    std::filesystem::remove(input_path, ec);
    std::filesystem::remove(encoded_path, ec);
    std::filesystem::remove(decoded_path, ec);
    std::filesystem::remove(preview_path, ec);
    return 0;
}

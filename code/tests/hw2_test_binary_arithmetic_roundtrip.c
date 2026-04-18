#include "hw2/hw2_codec.h"
#include "test_helpers.h"

int main(void)
{
    dic_hw2_codec_report report;
    dic_status status;

    status = dic_hw2_codec_run_file(
        "code/assets/Clinton's speech.txt",
        dic_hw2_binary_arithmetic_codec_backend,
        &report
    );

    DIC_EXPECT(status == DIC_STATUS_OK);
    DIC_EXPECT(report.roundtrip_matches);
    DIC_EXPECT(report.original_bytes > 0);
    DIC_EXPECT(report.decoded_bytes == report.original_bytes);
    DIC_EXPECT(report.encoded_bits > 0);
    DIC_EXPECT(report.encoded_bytes > 0);
    DIC_EXPECT(report.active_symbols == 3);
    DIC_EXPECT(report.compression_ratio > 0.0);

    return 0;
}

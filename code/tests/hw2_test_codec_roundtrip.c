#include "hw2/hw2_codec.h"
#include "test_helpers.h"

int main(void)
{
    dic_hw2_codec_report report;
    dic_hw2_codec_status status;

    status = dic_hw2_codec_run_file(
        "code/assets/Clinton's speech.txt",
        dic_hw2_huffman_codec_backend,
        &report
    );

    DIC_EXPECT(status == DIC_HW2_CODEC_OK);
    DIC_EXPECT(report.roundtrip_matches);
    DIC_EXPECT(report.original_bytes > 0);
    DIC_EXPECT(report.encoded_bits > 0);
    DIC_EXPECT(report.average_code_length > 0.0);
    DIC_EXPECT(report.compression_ratio > 0.0);
    DIC_EXPECT(report.compression_ratio < 1.0);
    DIC_EXPECT(report.analysis.unique_symbols > 0);
    DIC_EXPECT(report.active_symbols == report.analysis.unique_symbols);

    return 0;
}

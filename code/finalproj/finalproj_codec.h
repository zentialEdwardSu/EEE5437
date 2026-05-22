#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define FINALPROJ_BITSTREAM_PATH "image.bit"
#define FINALPROJ_TILED_BITSTREAM_PATH "image_tiled.bit"
#define FINALPROJ_SNR_BITSTREAM_PATH "image_snr.bit"
#define FINALPROJ_ROI_BITSTREAM_PATH "image_roi.bit"
#define FINALPROJ_RECON_GRAY_PATH "image_recon.pgm"
#define FINALPROJ_RECON_RGB_PATH "image_recon.ppm"
#define FINALPROJ_LEVELS 5

double imageEncoder(const char *orgImageFileName, int quantizationStepSize);
double imageDecoder(
    const char *bitstreamFileName,
    int quantizationStepSize,
    const char *orgImageFileName
);

double imageEncoderTiled(
    const char *orgImageFileName,
    int quantizationStepSize,
    int tileSize
);

double imageDecoderTiled(
    const char *bitstreamFileName,
    int quantizationStepSize,
    const char *orgImageFileName
);

double imageEncoderSNR(
    const char *orgImageFileName,
    int quantizationStepSize
);

double imageDecoderSNR(
    const char *bitstreamFileName,
    int quantizationStepSize,
    int decodedBitplanes,
    const char *orgImageFileName
);

double imageEncoderROI(
    const char *orgImageFileName,
    int quantizationStepSize
);

double imageDecoderROI(
    const char *bitstreamFileName,
    int quantizationStepSize,
    int decodedBitplanes,
    const char *orgImageFileName
);

int imageWriteJ2KStub(
    const char *orgImageFileName,
    const char *outputFileName
);

int imageWriteJ2K(
    const char *orgImageFileName,
    const char *outputFileName
);

int imageWriteJP2Stub(
    const char *orgImageFileName,
    const char *outputFileName
);

int imageWriteJP2(
    const char *orgImageFileName,
    const char *outputFileName
);

int imageReadJ2KInfo(const char *inputFileName);
int imageReadJP2Info(const char *inputFileName);

#ifdef __cplusplus
}
#endif

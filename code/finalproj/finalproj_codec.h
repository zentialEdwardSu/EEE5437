#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define FINALPROJ_BITSTREAM_PATH "image.bit"
#define FINALPROJ_RECON_GRAY_PATH "image_recon.pgm"
#define FINALPROJ_RECON_RGB_PATH "image_recon.ppm"
#define FINALPROJ_LEVELS 5

/** Encodes the assignment basic codec bitstream and returns bitrate in bits per pixel. */
double imageEncoder(const char *orgImageFileName, int quantizationStepSize);

/** Decodes the assignment basic codec bitstream and returns PSNR against the original image. */
double imageDecoder(
    const char *bitstreamFileName,
    int quantizationStepSize,
    const char *orgImageFileName
);

/** Writes a minimal raw J2K codestream stub for metadata and parser demos. */
int imageWriteJ2KStub(
    const char *orgImageFileName,
    const char *outputFileName
);

/** Writes a raw J2K codestream; quality -1 is reversible, 1..100 is irreversible 9/7. */
int imageWriteJ2K(
    const char *orgImageFileName,
    const char *outputFileName,
    int quality
);

/** Writes a minimal JP2 wrapper for metadata and parser demos. */
int imageWriteJP2Stub(
    const char *orgImageFileName,
    const char *outputFileName
);

/** Writes a JP2 file; quality -1 is reversible, 1..100 is irreversible 9/7. */
int imageWriteJP2(
    const char *orgImageFileName,
    const char *outputFileName,
    int quality
);

/** Writes a tiled JP2 file with the requested JPEG 2000 quality layer count. */
int imageWriteJP2Tiled(
    const char *orgImageFileName,
    const char *outputFileName,
    int tileSize,
    int layers
);

/** Decodes a raw J2K codestream and writes the reconstructed PGM or PPM image. */
int imageReadJ2K(
    const char *inputFileName,
    const char *outputFileName
);

/** Decodes the first maxLayers quality layers of a raw J2K codestream and writes PGM or PPM output. */
int imageReadJ2KLayers(
    const char *inputFileName,
    const char *outputFileName,
    int maxLayers
);

/** Decodes a JP2 file and writes the reconstructed PGM or PPM image. */
int imageReadJP2(
    const char *inputFileName,
    const char *outputFileName
);

/** Decodes the first maxLayers quality layers of a JP2 file and writes PGM or PPM output. */
int imageReadJP2Layers(
    const char *inputFileName,
    const char *outputFileName,
    int maxLayers
);

/** Prints raw J2K codestream metadata to stdout. */
int imageReadJ2KInfo(const char *inputFileName);

/** Prints JP2 codestream metadata to stdout. */
int imageReadJP2Info(const char *inputFileName);

#ifdef __cplusplus
}
#endif

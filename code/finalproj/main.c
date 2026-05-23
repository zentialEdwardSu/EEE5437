#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "finalproj/finalproj_codec.h"

static void finalproj_print_usage(void)
{
    fprintf(
        stderr,
        "usage:\n"
        "  finalproj encode <input.pgm|input.ppm> <q>\n"
        "  finalproj decode image.bit <q> <original.pgm|original.ppm>\n"
        "  finalproj codec <input.pgm|input.ppm> <q>\n"
        "  finalproj tile-encode <input.pgm|input.ppm> <q> <tile-size>\n"
        "  finalproj tile-decode image_tiled.bit <q> <original.pgm|original.ppm>\n"
        "  finalproj tile-codec <input.pgm|input.ppm> <q> <tile-size>\n"
        "  finalproj snr-encode <input.pgm|input.ppm> <q>\n"
        "  finalproj snr-decode image_snr.bit <q> <bitplanes> <original.pgm|original.ppm>\n"
        "  finalproj snr-codec <input.pgm|input.ppm> <q> <bitplanes>\n"
        "  finalproj roi-encode <input.pgm|input.ppm> <q>\n"
        "  finalproj roi-decode image_roi.bit <q> <bitplanes> <original.pgm|original.ppm>\n"
        "  finalproj roi-codec <input.pgm|input.ppm> <q> <bitplanes>\n"
        "  finalproj j2k-encode <input.pgm|input.ppm> <output.j2k>\n"
        "  finalproj jp2-encode <input.pgm|input.ppm> <output.jp2>\n"
        "  finalproj jp2-tile-encode <input.pgm|input.ppm> <output.jp2> <tile-size|auto> <layers>\n"
        "  finalproj j2k-decode <input.j2k> <output.pgm|output.ppm>\n"
        "  finalproj jp2-decode <input.jp2> <output.pgm|output.ppm>\n"
        "  finalproj j2k-decode-layer <input.j2k> <output.pgm|output.ppm> <layers-to-decode>\n"
        "  finalproj jp2-decode-layer <input.jp2> <output.pgm|output.ppm> <layers-to-decode>\n"
        "  finalproj j2k-stub <input.pgm|input.ppm> <output.j2k>\n"
        "  finalproj jp2-stub <input.pgm|input.ppm> <output.jp2>\n"
        "  finalproj j2k-info <input.j2k>\n"
        "  finalproj jp2-info <input.jp2>\n"
    );
}

static int finalproj_parse_positive_int(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    if (text == NULL || value == NULL)
        return 0;

    parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed <= 0 || parsed > 1000000L)
        return 0;

    *value = (int)parsed;
    return 1;
}

static int finalproj_parse_nonnegative_int(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    if (text == NULL || value == NULL)
        return 0;

    parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed < 0 || parsed > 1000000L)
        return 0;

    *value = (int)parsed;
    return 1;
}

static const char *finalproj_expected_reconstruction_path(const char *original_path)
{
    FILE *file = NULL;
    char magic[3] = {0, 0, 0};

#if defined(_MSC_VER)
    if (fopen_s(&file, original_path, "rb") != 0)
#else
    file = fopen(original_path, "rb");
#endif
    if (file == NULL)
        return "image_recon.pgm or image_recon.ppm";

    if (fread(magic, 1u, 2u, file) != 2u)
    {
        fclose(file);
        return "image_recon.pgm or image_recon.ppm";
    }
    fclose(file);

    if (strcmp(magic, "P5") == 0)
        return FINALPROJ_RECON_GRAY_PATH;
    if (strcmp(magic, "P6") == 0)
        return FINALPROJ_RECON_RGB_PATH;
    return "image_recon.pgm or image_recon.ppm";
}

int main(int argc, char **argv)
{
    int q = 0;

    if (argc < 2)
    {
        finalproj_print_usage();
        return 1;
    }

    if (strcmp(argv[1], "encode") == 0)
    {
        double bitrate;

        if (argc != 4 || !finalproj_parse_positive_int(argv[3], &q))
        {
            finalproj_print_usage();
            return 1;
        }

        bitrate = imageEncoder(argv[2], q);
        if (bitrate < 0.0)
        {
            fprintf(stderr, "error: failed to encode image\n");
            return 1;
        }

        printf("wrote %s\n", FINALPROJ_BITSTREAM_PATH);
        printf("Bitrate %.6f\n", bitrate);
        return 0;
    }

    if (strcmp(argv[1], "decode") == 0)
    {
        double psnr;

        if (argc != 5 || !finalproj_parse_positive_int(argv[3], &q))
        {
            finalproj_print_usage();
            return 1;
        }

        psnr = imageDecoder(argv[2], q, argv[4]);
        if (psnr < 0.0)
        {
            fprintf(stderr, "error: failed to decode image\n");
            return 1;
        }

        printf("PSNR %.6f\n", psnr);
        printf("wrote %s\n", finalproj_expected_reconstruction_path(argv[4]));
        return 0;
    }

    if (strcmp(argv[1], "codec") == 0)
    {
        double bitrate;
        double psnr;

        if (argc != 4 || !finalproj_parse_positive_int(argv[3], &q))
        {
            finalproj_print_usage();
            return 1;
        }

        bitrate = imageEncoder(argv[2], q);
        if (bitrate < 0.0)
        {
            fprintf(stderr, "error: failed to encode image\n");
            return 1;
        }

        psnr = imageDecoder(FINALPROJ_BITSTREAM_PATH, q, argv[2]);
        if (psnr < 0.0)
        {
            fprintf(stderr, "error: failed to decode image\n");
            return 1;
        }

        printf("Bitrate %.6f\n", bitrate);
        printf("PSNR %.6f\n", psnr);
        return 0;
    }

    if (strcmp(argv[1], "tile-encode") == 0)
    {
        int tile_size = 0;
        double bitrate;

        if (argc != 5
            || !finalproj_parse_positive_int(argv[3], &q)
            || !finalproj_parse_positive_int(argv[4], &tile_size))
        {
            finalproj_print_usage();
            return 1;
        }

        bitrate = imageEncoderTiled(argv[2], q, tile_size);
        if (bitrate < 0.0)
        {
            fprintf(stderr, "error: failed to tile-encode image\n");
            return 1;
        }

        printf("wrote %s\n", FINALPROJ_TILED_BITSTREAM_PATH);
        printf("Bitrate %.6f\n", bitrate);
        return 0;
    }

    if (strcmp(argv[1], "tile-decode") == 0)
    {
        double psnr;

        if (argc != 5 || !finalproj_parse_positive_int(argv[3], &q))
        {
            finalproj_print_usage();
            return 1;
        }

        psnr = imageDecoderTiled(argv[2], q, argv[4]);
        if (psnr < 0.0)
        {
            fprintf(stderr, "error: failed to tile-decode image\n");
            return 1;
        }

        printf("PSNR %.6f\n", psnr);
        printf("wrote %s\n", finalproj_expected_reconstruction_path(argv[4]));
        return 0;
    }

    if (strcmp(argv[1], "tile-codec") == 0)
    {
        int tile_size = 0;
        double bitrate;
        double psnr;

        if (argc != 5
            || !finalproj_parse_positive_int(argv[3], &q)
            || !finalproj_parse_positive_int(argv[4], &tile_size))
        {
            finalproj_print_usage();
            return 1;
        }

        bitrate = imageEncoderTiled(argv[2], q, tile_size);
        if (bitrate < 0.0)
        {
            fprintf(stderr, "error: failed to tile-encode image\n");
            return 1;
        }

        psnr = imageDecoderTiled(FINALPROJ_TILED_BITSTREAM_PATH, q, argv[2]);
        if (psnr < 0.0)
        {
            fprintf(stderr, "error: failed to tile-decode image\n");
            return 1;
        }

        printf("Bitrate %.6f\n", bitrate);
        printf("PSNR %.6f\n", psnr);
        return 0;
    }

    if (strcmp(argv[1], "snr-encode") == 0)
    {
        double bitrate;

        if (argc != 4 || !finalproj_parse_positive_int(argv[3], &q))
        {
            finalproj_print_usage();
            return 1;
        }

        bitrate = imageEncoderSNR(argv[2], q);
        if (bitrate < 0.0)
        {
            fprintf(stderr, "error: failed to snr-encode image\n");
            return 1;
        }

        printf("wrote %s\n", FINALPROJ_SNR_BITSTREAM_PATH);
        printf("Bitrate %.6f\n", bitrate);
        return 0;
    }

    if (strcmp(argv[1], "snr-decode") == 0)
    {
        int bitplanes = 0;
        double psnr;

        if (argc != 6
            || !finalproj_parse_positive_int(argv[3], &q)
            || !finalproj_parse_nonnegative_int(argv[4], &bitplanes))
        {
            finalproj_print_usage();
            return 1;
        }

        psnr = imageDecoderSNR(argv[2], q, bitplanes, argv[5]);
        if (psnr < 0.0)
        {
            fprintf(stderr, "error: failed to snr-decode image\n");
            return 1;
        }

        printf("PSNR %.6f\n", psnr);
        printf("wrote %s\n", finalproj_expected_reconstruction_path(argv[5]));
        return 0;
    }

    if (strcmp(argv[1], "snr-codec") == 0)
    {
        int bitplanes = 0;
        double bitrate;
        double psnr;

        if (argc != 5
            || !finalproj_parse_positive_int(argv[3], &q)
            || !finalproj_parse_nonnegative_int(argv[4], &bitplanes))
        {
            finalproj_print_usage();
            return 1;
        }

        bitrate = imageEncoderSNR(argv[2], q);
        if (bitrate < 0.0)
        {
            fprintf(stderr, "error: failed to snr-encode image\n");
            return 1;
        }

        psnr = imageDecoderSNR(FINALPROJ_SNR_BITSTREAM_PATH, q, bitplanes, argv[2]);
        if (psnr < 0.0)
        {
            fprintf(stderr, "error: failed to snr-decode image\n");
            return 1;
        }

        printf("Bitrate %.6f\n", bitrate);
        printf("PSNR %.6f\n", psnr);
        return 0;
    }

    if (strcmp(argv[1], "roi-encode") == 0)
    {
        double bitrate;

        if (argc != 4 || !finalproj_parse_positive_int(argv[3], &q))
        {
            finalproj_print_usage();
            return 1;
        }

        bitrate = imageEncoderROI(argv[2], q);
        if (bitrate < 0.0)
        {
            fprintf(stderr, "error: failed to roi-encode image\n");
            return 1;
        }

        printf("wrote %s\n", FINALPROJ_ROI_BITSTREAM_PATH);
        printf("Bitrate %.6f\n", bitrate);
        return 0;
    }

    if (strcmp(argv[1], "roi-decode") == 0)
    {
        int bitplanes = 0;
        double psnr;

        if (argc != 6
            || !finalproj_parse_positive_int(argv[3], &q)
            || !finalproj_parse_nonnegative_int(argv[4], &bitplanes))
        {
            finalproj_print_usage();
            return 1;
        }

        psnr = imageDecoderROI(argv[2], q, bitplanes, argv[5]);
        if (psnr < 0.0)
        {
            fprintf(stderr, "error: failed to roi-decode image\n");
            return 1;
        }

        printf("PSNR %.6f\n", psnr);
        printf("wrote %s\n", finalproj_expected_reconstruction_path(argv[5]));
        return 0;
    }

    if (strcmp(argv[1], "roi-codec") == 0)
    {
        int bitplanes = 0;
        double bitrate;
        double psnr;

        if (argc != 5
            || !finalproj_parse_positive_int(argv[3], &q)
            || !finalproj_parse_nonnegative_int(argv[4], &bitplanes))
        {
            finalproj_print_usage();
            return 1;
        }

        bitrate = imageEncoderROI(argv[2], q);
        if (bitrate < 0.0)
        {
            fprintf(stderr, "error: failed to roi-encode image\n");
            return 1;
        }

        psnr = imageDecoderROI(FINALPROJ_ROI_BITSTREAM_PATH, q, bitplanes, argv[2]);
        if (psnr < 0.0)
        {
            fprintf(stderr, "error: failed to roi-decode image\n");
            return 1;
        }

        printf("Bitrate %.6f\n", bitrate);
        printf("PSNR %.6f\n", psnr);
        return 0;
    }

    if (strcmp(argv[1], "j2k-stub") == 0 || strcmp(argv[1], "j2k-encode") == 0)
    {
        int ok;

        if (argc != 4)
        {
            finalproj_print_usage();
            return 1;
        }

        ok = strcmp(argv[1], "j2k-stub") == 0
            ? imageWriteJ2KStub(argv[2], argv[3])
            : imageWriteJ2K(argv[2], argv[3]);
        if (!ok)
        {
            fprintf(stderr, "error: failed to write JPEG 2000 codestream\n");
            return 1;
        }

        printf("wrote %s\n", argv[3]);
        return 0;
    }

    if (strcmp(argv[1], "jp2-stub") == 0 || strcmp(argv[1], "jp2-encode") == 0)
    {
        int ok;

        if (argc != 4)
        {
            finalproj_print_usage();
            return 1;
        }

        ok = strcmp(argv[1], "jp2-stub") == 0
            ? imageWriteJP2Stub(argv[2], argv[3])
            : imageWriteJP2(argv[2], argv[3]);
        if (!ok)
        {
            fprintf(stderr, "error: failed to write JP2 file\n");
            return 1;
        }

        printf("wrote %s\n", argv[3]);
        return 0;
    }

    if (strcmp(argv[1], "jp2-tile-encode") == 0)
    {
        int tile_size = 0;
        int layers = 0;

        if (argc != 6
            || (strcmp(argv[4], "auto") != 0 && !finalproj_parse_positive_int(argv[4], &tile_size))
            || !finalproj_parse_positive_int(argv[5], &layers))
        {
            finalproj_print_usage();
            return 1;
        }

        if (!imageWriteJP2Tiled(argv[2], argv[3], tile_size, layers))
        {
            fprintf(stderr, "error: failed to write tiled JP2 file\n");
            return 1;
        }

        printf("wrote %s\n", argv[3]);
        return 0;
    }

    if (strcmp(argv[1], "j2k-decode") == 0 || strcmp(argv[1], "jp2-decode") == 0)
    {
        int ok;

        if (argc != 4)
        {
            finalproj_print_usage();
            return 1;
        }

        ok = strcmp(argv[1], "j2k-decode") == 0
            ? imageReadJ2K(argv[2], argv[3])
            : imageReadJP2(argv[2], argv[3]);
        if (!ok)
        {
            fprintf(stderr, "error: failed to decode JPEG 2000 image\n");
            return 1;
        }

        printf("wrote %s\n", argv[3]);
        return 0;
    }

    if (strcmp(argv[1], "j2k-decode-layer") == 0 || strcmp(argv[1], "jp2-decode-layer") == 0)
    {
        int ok;
        int layers = 0;

        if (argc != 5 || !finalproj_parse_positive_int(argv[4], &layers) || layers > 65535)
        {
            finalproj_print_usage();
            return 1;
        }

        ok = strcmp(argv[1], "j2k-decode-layer") == 0
            ? imageReadJ2KLayers(argv[2], argv[3], layers)
            : imageReadJP2Layers(argv[2], argv[3], layers);
        if (!ok)
        {
            fprintf(stderr, "error: failed to decode JPEG 2000 image layer prefix\n");
            return 1;
        }

        printf("wrote %s\n", argv[3]);
        return 0;
    }

    if (strcmp(argv[1], "j2k-info") == 0)
    {
        if (argc != 3)
        {
            finalproj_print_usage();
            return 1;
        }

        if (!imageReadJ2KInfo(argv[2]))
        {
            fprintf(stderr, "error: failed to read JPEG 2000 codestream info\n");
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "jp2-info") == 0)
    {
        if (argc != 3)
        {
            finalproj_print_usage();
            return 1;
        }

        if (!imageReadJP2Info(argv[2]))
        {
            fprintf(stderr, "error: failed to read JP2 codestream info\n");
            return 1;
        }
        return 0;
    }

    finalproj_print_usage();
    return 1;
}

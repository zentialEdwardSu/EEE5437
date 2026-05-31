/**
 * @file main.c
 * @brief Command-line entrypoint for the final project basic codec and JPEG 2000 helpers.
 */

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
        "  finalproj j2k-encode <input.pgm|input.ppm> <output.j2k> [Q|-1]\n"
        "  finalproj jp2-encode <input.pgm|input.ppm> <output.jp2> [Q|-1]\n"
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

static int finalproj_parse_j2k_quality(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    if (text == NULL || value == NULL)
        return 0;

    parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0')
        return 0;
    if (parsed != -1L && (parsed < 1L || parsed > 100L))
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
    if (file == NULL)
#endif
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

static int finalproj_run_basic_command(int argc, char **argv)
{
    int q = 0;

    if (strcmp(argv[1], "encode") == 0)
    {
        double bitrate;

        if (argc != 4 || !finalproj_parse_positive_int(argv[3], &q))
            return 0;
        bitrate = imageEncoder(argv[2], q);
        if (bitrate < 0.0)
            return 0;
        printf("wrote %s\n", FINALPROJ_BITSTREAM_PATH);
        printf("Bitrate %.6f\n", bitrate);
        return 1;
    }

    if (strcmp(argv[1], "decode") == 0)
    {
        double psnr;

        if (argc != 5 || !finalproj_parse_positive_int(argv[3], &q))
            return 0;
        psnr = imageDecoder(argv[2], q, argv[4]);
        if (psnr < 0.0)
            return 0;
        printf("PSNR %.6f\n", psnr);
        printf("wrote %s\n", finalproj_expected_reconstruction_path(argv[4]));
        return 1;
    }

    if (strcmp(argv[1], "codec") == 0)
    {
        double bitrate;
        double psnr;

        if (argc != 4 || !finalproj_parse_positive_int(argv[3], &q))
            return 0;
        bitrate = imageEncoder(argv[2], q);
        if (bitrate < 0.0)
            return 0;
        psnr = imageDecoder(FINALPROJ_BITSTREAM_PATH, q, argv[2]);
        if (psnr < 0.0)
            return 0;
        printf("Bitrate %.6f\n", bitrate);
        printf("PSNR %.6f\n", psnr);
        return 1;
    }

    return -1;
}

static int finalproj_run_j2k_write_command(int argc, char **argv)
{
    int ok;
    int quality = -1;

    if (strcmp(argv[1], "j2k-stub") == 0 || strcmp(argv[1], "j2k-encode") == 0)
    {
        if ((strcmp(argv[1], "j2k-stub") == 0 && argc != 4)
            || (strcmp(argv[1], "j2k-encode") == 0 && argc != 4 && argc != 5))
            return 0;
        if (argc == 5 && !finalproj_parse_j2k_quality(argv[4], &quality))
            return 0;
        ok = strcmp(argv[1], "j2k-stub") == 0
            ? imageWriteJ2KStub(argv[2], argv[3])
            : imageWriteJ2K(argv[2], argv[3], quality);
        if (!ok)
            return 0;
        printf("wrote %s\n", argv[3]);
        return 1;
    }

    if (strcmp(argv[1], "jp2-stub") == 0 || strcmp(argv[1], "jp2-encode") == 0)
    {
        if ((strcmp(argv[1], "jp2-stub") == 0 && argc != 4)
            || (strcmp(argv[1], "jp2-encode") == 0 && argc != 4 && argc != 5))
            return 0;
        if (argc == 5 && !finalproj_parse_j2k_quality(argv[4], &quality))
            return 0;
        ok = strcmp(argv[1], "jp2-stub") == 0
            ? imageWriteJP2Stub(argv[2], argv[3])
            : imageWriteJP2(argv[2], argv[3], quality);
        if (!ok)
            return 0;
        printf("wrote %s\n", argv[3]);
        return 1;
    }

    if (strcmp(argv[1], "jp2-tile-encode") == 0)
    {
        int tile_size = 0;
        int layers = 0;

        if (argc != 6
            || (strcmp(argv[4], "auto") != 0 && !finalproj_parse_positive_int(argv[4], &tile_size))
            || !finalproj_parse_positive_int(argv[5], &layers))
        {
            return 0;
        }
        if (!imageWriteJP2Tiled(argv[2], argv[3], tile_size, layers))
            return 0;
        printf("wrote %s\n", argv[3]);
        return 1;
    }

    return -1;
}

static int finalproj_run_j2k_read_command(int argc, char **argv)
{
    int ok;
    int layers = 0;

    if (strcmp(argv[1], "j2k-decode") == 0 || strcmp(argv[1], "jp2-decode") == 0)
    {
        if (argc != 4)
            return 0;
        ok = strcmp(argv[1], "j2k-decode") == 0
            ? imageReadJ2K(argv[2], argv[3])
            : imageReadJP2(argv[2], argv[3]);
        if (!ok)
            return 0;
        printf("wrote %s\n", argv[3]);
        return 1;
    }

    if (strcmp(argv[1], "j2k-decode-layer") == 0 || strcmp(argv[1], "jp2-decode-layer") == 0)
    {
        if (argc != 5 || !finalproj_parse_positive_int(argv[4], &layers) || layers > 65535)
            return 0;
        ok = strcmp(argv[1], "j2k-decode-layer") == 0
            ? imageReadJ2KLayers(argv[2], argv[3], layers)
            : imageReadJP2Layers(argv[2], argv[3], layers);
        if (!ok)
            return 0;
        printf("wrote %s\n", argv[3]);
        return 1;
    }

    if (strcmp(argv[1], "j2k-info") == 0)
    {
        if (argc != 3)
            return 0;
        return imageReadJ2KInfo(argv[2]) ? 1 : 0;
    }

    if (strcmp(argv[1], "jp2-info") == 0)
    {
        if (argc != 3)
            return 0;
        return imageReadJP2Info(argv[2]) ? 1 : 0;
    }

    return -1;
}

int main(int argc, char **argv)
{
    int handled;

    if (argc < 2)
    {
        finalproj_print_usage();
        return 1;
    }

    handled = finalproj_run_basic_command(argc, argv);
    if (handled > 0)
        return 0;
    if (handled == 0)
    {
        fprintf(stderr, "error: failed to run basic codec command\n");
        finalproj_print_usage();
        return 1;
    }

    handled = finalproj_run_j2k_write_command(argc, argv);
    if (handled > 0)
        return 0;
    if (handled == 0)
    {
        fprintf(stderr, "error: failed to run JPEG 2000 write command\n");
        finalproj_print_usage();
        return 1;
    }

    handled = finalproj_run_j2k_read_command(argc, argv);
    if (handled > 0)
        return 0;
    if (handled == 0)
    {
        fprintf(stderr, "error: failed to run JPEG 2000 read command\n");
        finalproj_print_usage();
        return 1;
    }

    finalproj_print_usage();
    return 1;
}

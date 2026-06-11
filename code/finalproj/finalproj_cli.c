/**
 * @file finalproj_cli.c
 * @brief Implements the final project CLI with cargs option parsing.
 *
 * Two-level command hierarchy:
 *
 * @code{.unparsed}
 *   finalproj bit encode|decode|codec|send|receive|info [options]
 *   finalproj j2k write|write-tiled|read|info             [options]
 * @endcode
 */

#include "finalproj/finalproj_cli.h"

#include <cargs.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "finalproj/finalproj_codec.h"
#include "finalproj/finalproj_net.h"

/** @brief Container type inferred from a JPEG 2000 filename extension. */
typedef enum finalproj_j2k_container {
    finalproj_J2K_CONTAINER_UNKNOWN = 0,
    finalproj_J2K_CONTAINER_CODESTREAM = 1,
    finalproj_J2K_CONTAINER_JP2 = 2
} finalproj_j2k_container;

/**
 * @brief Borrowed option strings collected during one subcommand parse.
 *
 * Members point into argv storage and are never freed by this structure.
 */
typedef struct finalproj_cli_values {
    /** Borrowed `--input` value. */
    const char* input;
    /** Borrowed `--output` value. */
    const char* output;
    /** Borrowed `--bitstream` value. */
    const char* bitstream;
    /** Borrowed `--original` reference-image value. */
    const char* original;
    /** Borrowed `--tile-size` value. */
    const char* tile_size;
    /** Borrowed JPEG 2000 `--quality` value. */
    const char* quality;
    /** Borrowed basic-codec `--quant` value. */
    const char* quant;
    /** Borrowed `--layers` value. */
    const char* layers;
    /** Borrowed `--host` value. */
    const char* host;
    /** Borrowed `--port` value before numeric conversion. */
    const char* port_str;
    /** Borrowed `--rate` value before numeric conversion. */
    const char* rate;
} finalproj_cli_values;

/** @brief Prints the top-level command list to stderr. */
static void finalproj_print_usage(void) {
    fprintf(
        stderr,
        "usage:\n"
        "  finalproj bit encode   --input <file> --quant <q> [--output "
        "<file>]\n"
        "  finalproj bit decode   --bitstream <file> --original "
        "<file>\n"
        "  finalproj bit codec    --input <file> --quant <q>\n"
        "  finalproj bit send     --input <file> --host <HOST> --port <PORT> "
        "--quant <q> [--rate <BYTES/SEC>]\n"
        "  finalproj bit receive  --port <PORT> --output <file> "
        "[--original <file>]\n"
        "  finalproj bit info     --bitstream <file>\n"
#if WITH_J2K
        "  finalproj j2k write        --input <file> --output <file> "
        "[--quality <Q|-1>]\n"
        "  finalproj j2k write-tiled  --input <file> --output <file> "
        "--tile-size <N|auto> --layers <N>\n"
        "  finalproj j2k read         --input <file> --output <file> "
        "[--layers <N>]\n"
        "  finalproj j2k info         --input <file>\n"
        "\n"
#endif
        "Use `finalproj <group> <command> --help` for command options.\n");
}

/*  Shared utilities                                                         */

#if WITH_J2K
/** @brief Converts one ASCII letter to lowercase without locale state. */
static char finalproj_ascii_lower(char value) {
    if (value >= 'A' && value <= 'Z') return (char)(value - 'A' + 'a');
    return value;
}

/** @brief Performs a case-insensitive ASCII filename suffix check. */
static int finalproj_has_extension(const char* path, const char* extension) {
    size_t path_length, extension_length, offset, index;
    if (path == NULL || extension == NULL) return 0;
    path_length = strlen(path);
    extension_length = strlen(extension);
    if (path_length < extension_length) return 0;
    offset = path_length - extension_length;
    for (index = 0u; index < extension_length; ++index) {
        if (finalproj_ascii_lower(path[offset + index]) !=
            finalproj_ascii_lower(extension[index]))
            return 0;
    }
    return 1;
}
/** @brief Maps `.j2k` and `.jp2` suffixes to decoder container types. */
static finalproj_j2k_container finalproj_j2k_container_from_path(
    const char* path) {
    if (finalproj_has_extension(path, ".j2k"))
        return finalproj_J2K_CONTAINER_CODESTREAM;
    if (finalproj_has_extension(path, ".jp2"))
        return finalproj_J2K_CONTAINER_JP2;
    return finalproj_J2K_CONTAINER_UNKNOWN;
}

/** @brief Parses reversible `-1` or irreversible quality 1 through 100. */
static int finalproj_parse_quality(const char* text, int* value) {
    if (text == NULL) {
        *value = -1;
        return 1;
    }
    if (!finalproj_parse_int_range(text, -1, 100, value)) return 0;
    return *value == -1 || *value >= 1;
}
#endif /* WITH_J2K */

/**
 * @brief Parses a base-10 integer and validates an inclusive range.
 */
static int finalproj_parse_int_range(const char* text, int min_value,
                                     int max_value, int* value) {
    char* end = NULL;
    long parsed;
    if (text == NULL || value == NULL) return 0;
    parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed < min_value || parsed > max_value)
        return 0;
    *value = (int)parsed;
    return 1;
}

/** @brief Parses a finite positive quantization step. */
static int finalproj_parse_quant(const char* text, float* value) {
    char* end = NULL;
    float parsed;
    if (text == NULL || value == NULL) return 0;
    parsed = strtof(text, &end);
    if (end == text || *end != '\0' || !isfinite(parsed) || parsed <= 0.0f ||
        parsed > 1000000.0f)
        return 0;
    *value = parsed;
    return 1;
}

/** @brief Prints usage for one second-level command. */
static void finalproj_print_command_usage(const char* command,
                                          const cag_option* options,
                                          size_t option_count) {
    fprintf(stderr, "usage: finalproj %s [options]\n", command);
    cag_option_print(options, option_count, stderr);
}

/**
 * @brief Routes one cargs identifier into the shared option structure.
 */
static int finalproj_store_option(finalproj_cli_values* values, char identifier,
                                  const char* value) {
    if (values == NULL) return 0;
    switch (identifier) {
        case 'i':
            values->input = value;
            return 1;
        case 'o':
            values->output = value;
            return 1;
        case 'b':
            values->bitstream = value;
            return 1;
        case 'r':
            values->original = value;
            return 1;
        case 't':
            values->tile_size = value;
            return 1;
        case 'q':
            values->quality = value;
            values->quant = value;
            return 1;
        case 'l':
            values->layers = value;
            return 1;
        case 'H':
            values->host = value;
            return 1;
        case 'p':
            values->port_str = value;
            return 1;
        case 'R':
            values->rate = value;
            return 1;
        default:
            return 0;
    }
}

/**
 * @brief Runs cargs parsing for one command and rejects positional leftovers.
 *
 * @return 1 for parsed options, 0 for an error, and -1 after printing help.
 */
static int finalproj_parse_command(int argc, char** argv, const char* command,
                                   const cag_option* options,
                                   size_t option_count,
                                   finalproj_cli_values* values) {
    cag_option_context context;
    memset(values, 0, sizeof(*values));
    cag_option_init(&context, options, option_count, argc, argv);
    while (cag_option_fetch(&context)) {
        char identifier = cag_option_get_identifier(&context);
        if (identifier == 'h') {
            finalproj_print_command_usage(command, options, option_count);
            return -1;
        }
        if (!finalproj_store_option(values, identifier,
                                    cag_option_get_value(&context))) {
            cag_option_print_error(&context, stderr);
            finalproj_print_command_usage(command, options, option_count);
            return 0;
        }
    }
    if (cag_option_get_index(&context) < argc) {
        fprintf(stderr, "error: unexpected argument: %s\n",
                argv[cag_option_get_index(&context)]);
        finalproj_print_command_usage(command, options, option_count);
        return 0;
    }
    return 1;
}

/**
 * @brief Reads the PGM/PPM magic to select the wrapper reconstruction path.
 */
static const char* finalproj_expected_reconstruction_path(
    const char* original_path) {
    FILE* file = NULL;
    char magic[3] = {0, 0, 0};
#if defined(_MSC_VER)
    if (fopen_s(&file, original_path, "rb") != 0)
#else
    file = fopen(original_path, "rb");
    if (file == NULL)
#endif
        return "image_recon.pgm or image_recon.ppm";
    if (fread(magic, 1u, 2u, file) != 2u) {
        fclose(file);
        return "image_recon.pgm or image_recon.ppm";
    }
    fclose(file);
    if (strcmp(magic, "P5") == 0) return FINALPROJ_RECON_GRAY_PATH;
    if (strcmp(magic, "P6") == 0) return FINALPROJ_RECON_RGB_PATH;
    return "image_recon.pgm or image_recon.ppm";
}

/*  bit encode / decode / codec / info                                       */

/** @brief Handles `bit encode`. */
static int finalproj_bit_encode(int argc, char** argv) {
    const cag_option options[] = {
        {'i', "i", "input", "FILE", "input PGM or PPM image"},
        {'o', "o", "output", "FILE",
         "output bitstream file (default: image.bit)"},
        {'q', "q", "quant", "VALUE", "positive quantization step"},
        {'h', "h", "help", NULL, "show this help"}};
    finalproj_cli_values values;
    const char* output_path;
    double bitrate;
    float q = 0.0f;
    int parse_result = finalproj_parse_command(
        argc, argv, "bit encode", options, CAG_ARRAY_SIZE(options), &values);
    if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
    if (values.input == NULL || !finalproj_parse_quant(values.quant, &q)) {
        fprintf(stderr, "error: --input and positive --quant are required\n");
        return 1;
    }
    output_path =
        values.output != NULL ? values.output : FINALPROJ_BITSTREAM_PATH;
    bitrate = imageEncoder(values.input, q, output_path);
    if (bitrate < 0.0) {
        fprintf(stderr, "error: encode command failed\n");
        return 1;
    }
    printf("wrote %s\n", output_path);
    printf("Bitrate %.6f\n", bitrate);
    return 0;
}

/** @brief Handles `bit decode`. */
static int finalproj_bit_decode(int argc, char** argv) {
    const cag_option options[] = {
        {'b', "b", "bitstream", "FILE", "input basic codec bitstream"},
        {'r', "r", "original", "FILE", "original PGM or PPM image"},
        {'h', "h", "help", NULL, "show this help"}};
    finalproj_cli_values values;
    double psnr;
    int parse_result = finalproj_parse_command(
        argc, argv, "bit decode", options, CAG_ARRAY_SIZE(options), &values);
    if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
    if (values.bitstream == NULL || values.original == NULL) {
        fprintf(stderr, "error: --bitstream and --original are required\n");
        return 1;
    }
    psnr = imageDecoder(values.bitstream, values.original);
    if (psnr < 0.0) {
        fprintf(stderr, "error: decode command failed\n");
        return 1;
    }
    printf("PSNR %.6f\n", psnr);
    printf("wrote %s\n",
           finalproj_expected_reconstruction_path(values.original));
    return 0;
}

/** @brief Handles the encode/decode `bit codec` round trip. */
static int finalproj_bit_codec(int argc, char** argv) {
    const cag_option options[] = {
        {'i', "i", "input", "FILE", "input PGM or PPM image"},
        {'q', "q", "quant", "VALUE", "positive quantization step"},
        {'h', "h", "help", NULL, "show this help"}};
    finalproj_cli_values values;
    finalproj_codec_report report;
    const char* output_path;
    float q = 0.0f;
    int parse_result = finalproj_parse_command(
        argc, argv, "bit codec", options, CAG_ARRAY_SIZE(options), &values);
    if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
    if (values.input == NULL || !finalproj_parse_quant(values.quant, &q)) {
        fprintf(stderr, "error: --input and positive --quant are required\n");
        return 1;
    }
    output_path = FINALPROJ_BITSTREAM_PATH;
    if (!imageCodecReport(values.input, q, output_path, &report)) {
        fprintf(stderr, "error: codec roundtrip failed\n");
        return 1;
    }
    printf("{\"q\":%.9g,\"bitrate\":%.9g,\"compression_ratio\":%.9g,", q,
           report.bitrate, report.compression_ratio);
    if (isinf(report.psnr))
        printf("\"psnr\":null,\"lossless\":true,");
    else
        printf("\"psnr\":%.9g,\"lossless\":false,", report.psnr);
    printf("\"huffman_counts\":[%zu,%zu,%zu,%zu],",
           report.huffman_symbol_counts[0], report.huffman_symbol_counts[1],
           report.huffman_symbol_counts[2], report.huffman_symbol_counts[3]);
    printf("\"huffman_probabilities\":[%.12g,%.12g,%.12g,%.12g]}\n",
           report.huffman_probabilities[0], report.huffman_probabilities[1],
           report.huffman_probabilities[2], report.huffman_probabilities[3]);
    return 0;
}

/** @brief Handles DICW metadata inspection. */
static int finalproj_bit_info(int argc, char** argv) {
    const cag_option options[] = {
        {'b', "b", "bitstream", "FILE", "input basic codec bitstream"},
        {'h', "h", "help", NULL, "show this help"}};
    finalproj_cli_values values;
    int parse_result = finalproj_parse_command(
        argc, argv, "bit info", options, CAG_ARRAY_SIZE(options), &values);
    if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
    if (values.bitstream == NULL) {
        fprintf(stderr, "error: --bitstream is required\n");
        return 1;
    }
    if (!imageReadBitInfo(values.bitstream)) {
        fprintf(stderr, "error: bit info command failed\n");
        return 1;
    }
    return 0;
}

/*  bit send / receive via TCP by libnet                       */

/** @brief Handles staged DICW encoding and TCP transmission. */
static int finalproj_bit_send(int argc, char** argv) {
    const cag_option options[] = {
        {'i', "i", "input", "FILE", "input PGM or PPM image"},
        {'H', NULL, "host", "HOST", "destination hostname or IP address"},
        {'p', NULL, "port", "PORT", "destination port"},
        {'q', "q", "quant", "VALUE", "positive quantization step"},
        {'R', NULL, "rate", "BYTES/SEC",
         "optional send rate limit (0 = unlimited)"},
        {'h', "h", "help", NULL, "show this help"}};
    finalproj_cli_values values;
    float q = 0.0f;
    int port = 0;
    uint32_t rate_limit = 0u;
    int parse_result = finalproj_parse_command(
        argc, argv, "bit send", options, CAG_ARRAY_SIZE(options), &values);
    if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
    if (values.input == NULL || values.host == NULL ||
        values.port_str == NULL || !finalproj_parse_quant(values.quant, &q)) {
        fprintf(stderr,
                "error: --input, --host, --port, and positive --quant are "
                "required\n");
        return 1;
    }
    if (!finalproj_parse_int_range(values.port_str, 1, 65535, &port)) {
        fprintf(stderr, "error: --port must be in [1, 65535]\n");
        return 1;
    }
    if (values.rate != NULL) {
        int rate_val = 0;
        if (!finalproj_parse_int_range(values.rate, 0, 1000000000, &rate_val)) {
            fprintf(stderr, "error: --rate must be a non-negative integer\n");
            return 1;
        }
        rate_limit = (uint32_t)rate_val;
    }
    if (!networkSend(values.input, values.host, port, q, rate_limit)) {
        fprintf(stderr, "error: send command failed\n");
        return 1;
    }
    return 0;
}

/** @brief Handles incremental DICW TCP reception and reconstruction. */
static int finalproj_bit_receive(int argc, char** argv) {
    const cag_option options[] = {
        {'p', NULL, "port", "PORT", "listen port"},
        {'o', "o", "output", "FILE", "output PGM or PPM image"},
        {'r', "r", "original", "FILE",
         "optional original image for PSNR comparison"},
        {'h', "h", "help", NULL, "show this help"}};
    finalproj_cli_values values;
    int port = 0;
    int parse_result = finalproj_parse_command(
        argc, argv, "bit receive", options, CAG_ARRAY_SIZE(options), &values);
    if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
    if (values.port_str == NULL || values.output == NULL) {
        fprintf(stderr, "error: --port and --output are required\n");
        return 1;
    }
    if (!finalproj_parse_int_range(values.port_str, 1, 65535, &port)) {
        fprintf(stderr, "error: --port must be in [1, 65535]\n");
        return 1;
    }
    if (!networkReceive(port, values.output, values.original)) {
        fprintf(stderr, "error: receive command failed\n");
        return 1;
    }
    return 0;
}
#if WITH_J2K
/*  j2k write / write-tiled / read / info                                    */

static int finalproj_j2k_write(int argc, char** argv) {
    const cag_option options[] = {
        {'i', "i", "input", "FILE", "input PGM or PPM image"},
        {'o', "o", "output", "FILE", "output .j2k or .jp2 file"},
        {'q', "q", "quality", "Q", "JPEG 2000 quality; -1 is reversible"},
        {'h', "h", "help", NULL, "show this help"}};
    finalproj_cli_values values;
    finalproj_j2k_container container;
    int quality = -1, ok = 0;
    int parse_result = finalproj_parse_command(
        argc, argv, "j2k write", options, CAG_ARRAY_SIZE(options), &values);
    if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
    if (values.input == NULL || values.output == NULL ||
        !finalproj_parse_quality(values.quality, &quality)) {
        fprintf(stderr,
                "error: --input, --output, and valid optional --quality are "
                "required\n");
        return 1;
    }
    container = finalproj_j2k_container_from_path(values.output);
    if (container == finalproj_J2K_CONTAINER_CODESTREAM)
        ok = imageWriteJ2K(values.input, values.output, quality);
    else if (container == finalproj_J2K_CONTAINER_JP2)
        ok = imageWriteJP2(values.input, values.output, quality);
    else
        fprintf(stderr, "error: --output must end in .j2k or .jp2\n");
    if (ok)
        printf("wrote %s\n", values.output);
    else
        fprintf(stderr, "error: j2k write command failed\n");
    return ok ? 0 : 1;
}

static int finalproj_j2k_write_tiled(int argc, char** argv) {
    const cag_option options[] = {
        {'i', "i", "input", "FILE", "input PGM or PPM image"},
        {'o', "o", "output", "FILE", "output .jp2 file"},
        {'t', NULL, "tile-size", "N|auto", "tile edge size or auto"},
        {'l', NULL, "layers", "N", "positive quality layer count"},
        {'h', "h", "help", NULL, "show this help"}};
    finalproj_cli_values values;
    int tile_size = 0, layers = 0, ok;
    int parse_result =
        finalproj_parse_command(argc, argv, "j2k write-tiled", options,
                                CAG_ARRAY_SIZE(options), &values);
    if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
    if (values.input == NULL || values.output == NULL ||
        values.tile_size == NULL ||
        !finalproj_parse_int_range(values.layers, 1, 1000000, &layers)) {
        fprintf(stderr,
                "error: --input, --output, --tile-size, and positive --layers "
                "are required\n");
        return 1;
    }
    if (finalproj_j2k_container_from_path(values.output) !=
        finalproj_J2K_CONTAINER_JP2) {
        fprintf(stderr, "error: --output must end in .jp2\n");
        return 1;
    }
    if (strcmp(values.tile_size, "auto") != 0 &&
        !finalproj_parse_int_range(values.tile_size, 1, 1000000, &tile_size)) {
        fprintf(stderr,
                "error: --tile-size must be auto or a positive integer\n");
        return 1;
    }
    ok = imageWriteJP2Tiled(values.input, values.output, tile_size, layers);
    if (ok)
        printf("wrote %s\n", values.output);
    else
        fprintf(stderr, "error: j2k write-tiled command failed\n");
    return ok ? 0 : 1;
}

static int finalproj_j2k_read(int argc, char** argv) {
    const cag_option options[] = {
        {'i', "i", "input", "FILE", "input .j2k or .jp2 file"},
        {'o', "o", "output", "FILE", "output PGM or PPM image"},
        {'l', NULL, "layers", "N", "optional quality layers to decode"},
        {'h', "h", "help", NULL, "show this help"}};
    finalproj_cli_values values;
    finalproj_j2k_container container;
    int layers = 0, ok = 0;
    int parse_result = finalproj_parse_command(
        argc, argv, "j2k read", options, CAG_ARRAY_SIZE(options), &values);
    if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
    if (values.input == NULL || values.output == NULL) {
        fprintf(stderr, "error: --input and --output are required\n");
        return 1;
    }
    if (values.layers != NULL &&
        !finalproj_parse_int_range(values.layers, 1, 65535, &layers)) {
        fprintf(stderr, "error: --layers must be in [1, 65535]\n");
        return 1;
    }
    container = finalproj_j2k_container_from_path(values.input);
    if (container == finalproj_J2K_CONTAINER_CODESTREAM)
        ok = imageReadJ2KLayers(values.input, values.output, layers);
    else if (container == finalproj_J2K_CONTAINER_JP2)
        ok = imageReadJP2Layers(values.input, values.output, layers);
    else
        fprintf(stderr, "error: --input must end in .j2k or .jp2\n");
    if (ok)
        printf("wrote %s\n", values.output);
    else
        fprintf(stderr, "error: j2k read command failed\n");
    return ok ? 0 : 1;
}

static int finalproj_j2k_info(int argc, char** argv) {
    const cag_option options[] = {
        {'i', "i", "input", "FILE", "input .j2k or .jp2 file"},
        {'h', "h", "help", NULL, "show this help"}};
    finalproj_cli_values values;
    finalproj_j2k_container container;
    int ok = 0;
    int parse_result = finalproj_parse_command(
        argc, argv, "j2k info", options, CAG_ARRAY_SIZE(options), &values);
    if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
    if (values.input == NULL) {
        fprintf(stderr, "error: --input is required\n");
        return 1;
    }
    container = finalproj_j2k_container_from_path(values.input);
    if (container == finalproj_J2K_CONTAINER_CODESTREAM)
        ok = imageReadJ2KInfo(values.input);
    else if (container == finalproj_J2K_CONTAINER_JP2)
        ok = imageReadJP2Info(values.input);
    else
        fprintf(stderr, "error: --input must end in .j2k or .jp2\n");
    if (!ok) fprintf(stderr, "error: j2k info command failed\n");
    return ok ? 0 : 1;
}
#endif /* WITH_J2K */
/*  Top-level dispatch                                                       */

/** @brief Dispatches one command in the `bit` command group. */
static int finalproj_dispatch_bit(int argc, char** argv) {
    if (argc < 1) goto unknown;
    if (strcmp(argv[0], "encode") == 0) return finalproj_bit_encode(argc, argv);
    if (strcmp(argv[0], "decode") == 0) return finalproj_bit_decode(argc, argv);
    if (strcmp(argv[0], "codec") == 0) return finalproj_bit_codec(argc, argv);
    if (strcmp(argv[0], "send") == 0) return finalproj_bit_send(argc, argv);
    if (strcmp(argv[0], "receive") == 0)
        return finalproj_bit_receive(argc, argv);
    if (strcmp(argv[0], "info") == 0) return finalproj_bit_info(argc, argv);
unknown:
    fprintf(stderr, "error: unknown bit subcommand: %s\n",
            argc >= 1 ? argv[0] : "(none)");
    return 1;
}

/** @brief Dispatches one command in the optional `j2k` command group. */
static int finalproj_dispatch_j2k(int argc, char** argv) {
#if WITH_J2K
    if (argc < 1) goto unknown;
    if (strcmp(argv[0], "write") == 0) return finalproj_j2k_write(argc, argv);
    if (strcmp(argv[0], "write-tiled") == 0)
        return finalproj_j2k_write_tiled(argc, argv);
    if (strcmp(argv[0], "read") == 0) return finalproj_j2k_read(argc, argv);
    if (strcmp(argv[0], "info") == 0) return finalproj_j2k_info(argc, argv);
unknown:
    fprintf(stderr, "error: unknown j2k subcommand: %s\n",
            argc >= 1 ? argv[0] : "(none)");
    return 1;
#else
    (void)argc;
    (void)argv;
    fprintf(stderr,
            "error: j2k commands are not available because build option "
            "`WITH_J2K` is not enabled\n");
    return 1;
#endif /* WITH_J2K */
}

/**
 * @brief Selects a top-level command group after removing argv[0].
 */
int finalproj_cli_run(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "-h") == 0) {
        finalproj_print_usage();
        return argc < 2 ? 1 : 0;
    }

    if (strcmp(argv[1], "bit") == 0)
        return finalproj_dispatch_bit(argc - 2, argv + 2);
    if (strcmp(argv[1], "j2k") == 0)
        return finalproj_dispatch_j2k(argc - 2, argv + 2);

    fprintf(stderr, "error: unknown command group: %s\n", argv[1]);
    finalproj_print_usage();
    return 1;
}

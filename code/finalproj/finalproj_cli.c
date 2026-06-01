/**
 * @file finalproj_cli.c
 * @brief Implements the final project CLI with cargs option parsing.
 */

#include "finalproj/finalproj_cli.h"

#include <cargs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "finalproj/finalproj_codec.h"

typedef enum finalproj_j2k_container
{
    finalproj_J2K_CONTAINER_UNKNOWN = 0,
    finalproj_J2K_CONTAINER_CODESTREAM = 1,
    finalproj_J2K_CONTAINER_JP2 = 2
} finalproj_j2k_container;

typedef struct finalproj_cli_values
{
    const char *input;
    const char *output;
    const char *bitstream;
    const char *original;
    const char *tile_size;
    const char *quality;
    const char *quant;
    const char *layers;
} finalproj_cli_values;

/**
 * Prints the top-level command list.
 */
static void finalproj_print_usage(void)
{
    fprintf(
        stderr,
        "usage:\n"
        "  finalproj encode --input <input.pgm|input.ppm> --quant <q>\n"
        "  finalproj decode --bitstream image.bit --quant <q> --original <original.pgm|original.ppm>\n"
        "  finalproj codec --input <input.pgm|input.ppm> --quant <q>\n"
        "  finalproj j2k-write --input <input.pgm|input.ppm> --output <output.j2k|output.jp2> [--quality <Q|-1>]\n"
        "  finalproj j2k-write-tiled --input <input.pgm|input.ppm> --output <output.jp2> --tile-size <N|auto> --layers <N>\n"
        "  finalproj j2k-read --input <input.j2k|input.jp2> --output <output.pgm|output.ppm> [--layers <N>]\n"
        "  finalproj j2k-info --input <input.j2k|input.jp2>\n"
        "\n"
        "Use `finalproj <command> --help` for command options.\n"
    );
}

/**
 * Converts an ASCII character to lowercase without locale-dependent behavior.
 */
static char finalproj_ascii_lower(char value)
{
    if (value >= 'A' && value <= 'Z')
        return (char)(value - 'A' + 'a');

    return value;
}

/**
 * Compares a filename suffix without relying on non-standard strcasecmp.
 */
static int finalproj_has_extension(const char *path, const char *extension)
{
    size_t path_length;
    size_t extension_length;
    size_t offset;
    size_t index;

    if (path == NULL || extension == NULL)
        return 0;

    path_length = strlen(path);
    extension_length = strlen(extension);
    if (path_length < extension_length)
        return 0;

    offset = path_length - extension_length;
    for (index = 0u; index < extension_length; ++index)
    {
        if (finalproj_ascii_lower(path[offset + index]) !=
            finalproj_ascii_lower(extension[index]))
        {
            return 0;
        }
    }

    return 1;
}

/**
 * Classifies JPEG 2000 files by the extension used by the CLI.
 */
static finalproj_j2k_container finalproj_j2k_container_from_path(const char *path)
{
    if (finalproj_has_extension(path, ".j2k"))
        return finalproj_J2K_CONTAINER_CODESTREAM;
    if (finalproj_has_extension(path, ".jp2"))
        return finalproj_J2K_CONTAINER_JP2;

    return finalproj_J2K_CONTAINER_UNKNOWN;
}

/**
 * Parses an integer from an option value and enforces a closed range.
 */
static int finalproj_parse_int_range(const char *text, int min_value, int max_value, int *value)
{
    char *end = NULL;
    long parsed;

    if (text == NULL || value == NULL)
        return 0;

    parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed < min_value || parsed > max_value)
        return 0;

    *value = (int)parsed;
    return 1;
}

/**
 * Parses the JPEG 2000 quality option, where -1 means reversible lossless mode.
 */
static int finalproj_parse_quality(const char *text, int *value)
{
    if (text == NULL)
    {
        *value = -1;
        return 1;
    }

    if (!finalproj_parse_int_range(text, -1, 100, value))
        return 0;
    return *value == -1 || *value >= 1;
}

/**
 * Prints command-specific option help.
 */
static void finalproj_print_command_usage(
    const char *command,
    const cag_option *options,
    size_t option_count
)
{
    fprintf(stderr, "usage: finalproj %s [options]\n", command);
    cag_option_print(options, option_count, stderr);
}

/**
 * Stores a parsed option into the generic command value object.
 */
static int finalproj_store_option(
    finalproj_cli_values *values,
    char identifier,
    const char *value
)
{
    if (values == NULL)
        return 0;

    switch (identifier)
    {
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
    default:
        return 0;
    }
}

/**
 * Parses cargs options for one command and rejects leftover positional args.
 */
static int finalproj_parse_command(
    int argc,
    char **argv,
    const char *command,
    const cag_option *options,
    size_t option_count,
    finalproj_cli_values *values
)
{
    cag_option_context context;

    memset(values, 0, sizeof(*values));
    cag_option_init(&context, options, option_count, argc, argv);
    while (cag_option_fetch(&context))
    {
        char identifier = cag_option_get_identifier(&context);

        if (identifier == 'h')
        {
            finalproj_print_command_usage(command, options, option_count);
            return -1;
        }
        if (!finalproj_store_option(values, identifier, cag_option_get_value(&context)))
        {
            cag_option_print_error(&context, stderr);
            finalproj_print_command_usage(command, options, option_count);
            return 0;
        }
    }

    if (cag_option_get_index(&context) < argc)
    {
        fprintf(stderr, "error: unexpected argument: %s\n", argv[cag_option_get_index(&context)]);
        finalproj_print_command_usage(command, options, option_count);
        return 0;
    }

    return 1;
}

/**
 * Returns the reconstruction filename that imageDecoder writes for the source image type.
 */
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

/**
 * Runs the basic bitstream encode command.
 */
static int finalproj_run_encode(int argc, char **argv)
{
    const cag_option options[] = {
        {'i', "i", "input", "FILE", "input PGM or PPM image"},
        {'q', "q", "quant", "N", "positive quantization step"},
        {'h', "h", "help", NULL, "show this help"}
    };
    finalproj_cli_values values;
    double bitrate;
    int q = 0;
    int parse_result = finalproj_parse_command(
        argc,
        argv,
        "encode",
        options,
        CAG_ARRAY_SIZE(options),
        &values
    );

    if (parse_result <= 0)
        return parse_result < 0 ? 0 : 1;
    if (values.input == NULL || !finalproj_parse_int_range(values.quant, 1, 1000000, &q))
    {
        fprintf(stderr, "error: --input and positive --quant are required\n");
        return 1;
    }

    bitrate = imageEncoder(values.input, q);
    if (bitrate < 0.0)
        return 1;

    printf("wrote %s\n", FINALPROJ_BITSTREAM_PATH);
    printf("Bitrate %.6f\n", bitrate);
    return 0;
}

/**
 * Runs the basic bitstream decode command.
 */
static int finalproj_run_decode(int argc, char **argv)
{
    const cag_option options[] = {
        {'b', "b", "bitstream", "FILE", "input basic codec bitstream"},
        {'q', "q", "quant", "N", "positive quantization step"},
        {'r', "r", "original", "FILE", "original PGM or PPM image"},
        {'h', "h", "help", NULL, "show this help"}
    };
    finalproj_cli_values values;
    double psnr;
    int q = 0;
    int parse_result = finalproj_parse_command(
        argc,
        argv,
        "decode",
        options,
        CAG_ARRAY_SIZE(options),
        &values
    );

    if (parse_result <= 0)
        return parse_result < 0 ? 0 : 1;
    if (values.bitstream == NULL || values.original == NULL ||
        !finalproj_parse_int_range(values.quant, 1, 1000000, &q))
    {
        fprintf(stderr, "error: --bitstream, --original, and positive --quant are required\n");
        return 1;
    }

    psnr = imageDecoder(values.bitstream, q, values.original);
    if (psnr < 0.0)
        return 1;

    printf("PSNR %.6f\n", psnr);
    printf("wrote %s\n", finalproj_expected_reconstruction_path(values.original));
    return 0;
}

/**
 * Runs a basic encode followed by decode for quick roundtrip inspection.
 */
static int finalproj_run_codec(int argc, char **argv)
{
    const cag_option options[] = {
        {'i', "i", "input", "FILE", "input PGM or PPM image"},
        {'q', "q", "quant", "N", "positive quantization step"},
        {'h', "h", "help", NULL, "show this help"}
    };
    finalproj_cli_values values;
    double bitrate;
    double psnr;
    int q = 0;
    int parse_result = finalproj_parse_command(
        argc,
        argv,
        "codec",
        options,
        CAG_ARRAY_SIZE(options),
        &values
    );

    if (parse_result <= 0)
        return parse_result < 0 ? 0 : 1;
    if (values.input == NULL || !finalproj_parse_int_range(values.quant, 1, 1000000, &q))
    {
        fprintf(stderr, "error: --input and positive --quant are required\n");
        return 1;
    }

    bitrate = imageEncoder(values.input, q);
    psnr = bitrate >= 0.0 ? imageDecoder(FINALPROJ_BITSTREAM_PATH, q, values.input) : -1.0;
    if (bitrate < 0.0 || psnr < 0.0)
        return 1;

    printf("Bitrate %.6f\n", bitrate);
    printf("PSNR %.6f\n", psnr);
    return 0;
}

/**
 * Runs JPEG 2000 codestream or JP2 writing based on the output extension.
 */
static int finalproj_run_j2k_write(int argc, char **argv)
{
    const cag_option options[] = {
        {'i', "i", "input", "FILE", "input PGM or PPM image"},
        {'o', "o", "output", "FILE", "output .j2k or .jp2 file"},
        {'q', "q", "quality", "Q", "JPEG 2000 quality; -1 is reversible"},
        {'h', "h", "help", NULL, "show this help"}
    };
    finalproj_cli_values values;
    finalproj_j2k_container container;
    int quality = -1;
    int ok = 0;
    int parse_result = finalproj_parse_command(
        argc,
        argv,
        "j2k-write",
        options,
        CAG_ARRAY_SIZE(options),
        &values
    );

    if (parse_result <= 0)
        return parse_result < 0 ? 0 : 1;
    if (values.input == NULL || values.output == NULL || !finalproj_parse_quality(values.quality, &quality))
    {
        fprintf(stderr, "error: --input, --output, and valid optional --quality are required\n");
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
    return ok ? 0 : 1;
}

/**
 * Runs tiled JP2 writing with explicit tile and layer controls.
 */
static int finalproj_run_j2k_write_tiled(int argc, char **argv)
{
    const cag_option options[] = {
        {'i', "i", "input", "FILE", "input PGM or PPM image"},
        {'o', "o", "output", "FILE", "output .jp2 file"},
        {'t', NULL, "tile-size", "N|auto", "tile edge size or auto"},
        {'l', NULL, "layers", "N", "positive quality layer count"},
        {'h', "h", "help", NULL, "show this help"}
    };
    finalproj_cli_values values;
    int tile_size = 0;
    int layers = 0;
    int ok;
    int parse_result = finalproj_parse_command(
        argc,
        argv,
        "j2k-write-tiled",
        options,
        CAG_ARRAY_SIZE(options),
        &values
    );

    if (parse_result <= 0)
        return parse_result < 0 ? 0 : 1;
    if (values.input == NULL || values.output == NULL || values.tile_size == NULL ||
        !finalproj_parse_int_range(values.layers, 1, 1000000, &layers))
    {
        fprintf(stderr, "error: --input, --output, --tile-size, and positive --layers are required\n");
        return 1;
    }
    if (finalproj_j2k_container_from_path(values.output) != finalproj_J2K_CONTAINER_JP2)
    {
        fprintf(stderr, "error: --output must end in .jp2\n");
        return 1;
    }
    if (strcmp(values.tile_size, "auto") != 0 &&
        !finalproj_parse_int_range(values.tile_size, 1, 1000000, &tile_size))
    {
        fprintf(stderr, "error: --tile-size must be auto or a positive integer\n");
        return 1;
    }

    ok = imageWriteJP2Tiled(values.input, values.output, tile_size, layers);
    if (ok)
        printf("wrote %s\n", values.output);
    return ok ? 0 : 1;
}

/**
 * Runs JPEG 2000 codestream or JP2 reading based on the input extension.
 */
static int finalproj_run_j2k_read(int argc, char **argv)
{
    const cag_option options[] = {
        {'i', "i", "input", "FILE", "input .j2k or .jp2 file"},
        {'o', "o", "output", "FILE", "output PGM or PPM image"},
        {'l', NULL, "layers", "N", "optional quality layers to decode"},
        {'h', "h", "help", NULL, "show this help"}
    };
    finalproj_cli_values values;
    finalproj_j2k_container container;
    int layers = 0;
    int ok = 0;
    int parse_result = finalproj_parse_command(
        argc,
        argv,
        "j2k-read",
        options,
        CAG_ARRAY_SIZE(options),
        &values
    );

    if (parse_result <= 0)
        return parse_result < 0 ? 0 : 1;
    if (values.input == NULL || values.output == NULL)
    {
        fprintf(stderr, "error: --input and --output are required\n");
        return 1;
    }
    if (values.layers != NULL && !finalproj_parse_int_range(values.layers, 1, 65535, &layers))
    {
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
    return ok ? 0 : 1;
}

/**
 * Prints JPEG 2000 codestream or JP2 metadata based on the input extension.
 */
static int finalproj_run_j2k_info(int argc, char **argv)
{
    const cag_option options[] = {
        {'i', "i", "input", "FILE", "input .j2k or .jp2 file"},
        {'h', "h", "help", NULL, "show this help"}
    };
    finalproj_cli_values values;
    finalproj_j2k_container container;
    int ok = 0;
    int parse_result = finalproj_parse_command(
        argc,
        argv,
        "j2k-info",
        options,
        CAG_ARRAY_SIZE(options),
        &values
    );

    if (parse_result <= 0)
        return parse_result < 0 ? 0 : 1;
    if (values.input == NULL)
    {
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

    return ok ? 0 : 1;
}

/**
 * Dispatches to the selected subcommand.
 */
int finalproj_cli_run(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
    {
        finalproj_print_usage();
        return argc < 2 ? 1 : 0;
    }

    if (strcmp(argv[1], "encode") == 0)
        return finalproj_run_encode(argc - 1, argv + 1);
    if (strcmp(argv[1], "decode") == 0)
        return finalproj_run_decode(argc - 1, argv + 1);
    if (strcmp(argv[1], "codec") == 0)
        return finalproj_run_codec(argc - 1, argv + 1);
    if (strcmp(argv[1], "j2k-write") == 0)
        return finalproj_run_j2k_write(argc - 1, argv + 1);
    if (strcmp(argv[1], "j2k-write-tiled") == 0)
        return finalproj_run_j2k_write_tiled(argc - 1, argv + 1);
    if (strcmp(argv[1], "j2k-read") == 0)
        return finalproj_run_j2k_read(argc - 1, argv + 1);
    if (strcmp(argv[1], "j2k-info") == 0)
        return finalproj_run_j2k_info(argc - 1, argv + 1);

    fprintf(stderr, "error: unknown command: %s\n", argv[1]);
    finalproj_print_usage();
    return 1;
}

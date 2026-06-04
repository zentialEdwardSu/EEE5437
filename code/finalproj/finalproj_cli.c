/**
 * @file finalproj_cli.c
 * @brief Implements the final project CLI with cargs option parsing.
 *
 * Two-level command hierarchy:
 *   finalproj bit encode|decode|codec|send|receive|info [options]
 *   finalproj j2k write|write-tiled|read|info             [options]
 */

#include "finalproj/finalproj_cli.h"

#include <cargs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "finalproj/finalproj_codec.h"
#include "finalproj/finalproj_net.h"

typedef enum finalproj_j2k_container {
  finalproj_J2K_CONTAINER_UNKNOWN = 0,
  finalproj_J2K_CONTAINER_CODESTREAM = 1,
  finalproj_J2K_CONTAINER_JP2 = 2
} finalproj_j2k_container;

typedef struct finalproj_cli_values {
  const char* input;
  const char* output;
  const char* bitstream;
  const char* original;
  const char* tile_size;
  const char* quality;
  const char* quant;
  const char* layers;
  const char* host;
  const char* port_str;
  const char* rate;
} finalproj_cli_values;

/**
 * Prints the top-level command list.
 */
static void finalproj_print_usage(void) {
  fprintf(stderr,
          "usage:\n"
          "  finalproj bit encode   --input <file> --quant <q> [--output <file>]\n"
          "  finalproj bit decode   --bitstream <file> --quant <q> --original <file>\n"
          "  finalproj bit codec    --input <file> --quant <q>\n"
          "  finalproj bit send     --input <file> --host <HOST> --port <PORT> "
          "--quant <q> [--rate <BYTES/SEC>]\n"
          "  finalproj bit receive  --port <PORT> --output <file> --quant <q> "
          "[--original <file>]\n"
          "  finalproj bit info     --bitstream <file>\n"
          "  finalproj j2k write        --input <file> --output <file> "
          "[--quality <Q|-1>]\n"
          "  finalproj j2k write-tiled  --input <file> --output <file> "
          "--tile-size <N|auto> --layers <N>\n"
          "  finalproj j2k read         --input <file> --output <file> "
          "[--layers <N>]\n"
          "  finalproj j2k info         --input <file>\n"
          "\n"
          "Use `finalproj <group> <command> --help` for command options.\n");
}

/* ========================================================================= */
/*  Shared utilities                                                         */
/* ========================================================================= */

static char finalproj_ascii_lower(char value) {
  if (value >= 'A' && value <= 'Z') return (char)(value - 'A' + 'a');
  return value;
}

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

static finalproj_j2k_container finalproj_j2k_container_from_path(
    const char* path) {
  if (finalproj_has_extension(path, ".j2k"))
    return finalproj_J2K_CONTAINER_CODESTREAM;
  if (finalproj_has_extension(path, ".jp2"))
    return finalproj_J2K_CONTAINER_JP2;
  return finalproj_J2K_CONTAINER_UNKNOWN;
}

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

static int finalproj_parse_quality(const char* text, int* value) {
  if (text == NULL) { *value = -1; return 1; }
  if (!finalproj_parse_int_range(text, -1, 100, value)) return 0;
  return *value == -1 || *value >= 1;
}

static void finalproj_print_command_usage(const char* command,
                                          const cag_option* options,
                                          size_t option_count) {
  fprintf(stderr, "usage: finalproj %s [options]\n", command);
  cag_option_print(options, option_count, stderr);
}

static int finalproj_store_option(finalproj_cli_values* values,
                                  char identifier, const char* value) {
  if (values == NULL) return 0;
  switch (identifier) {
    case 'i': values->input = value;     return 1;
    case 'o': values->output = value;    return 1;
    case 'b': values->bitstream = value; return 1;
    case 'r': values->original = value;  return 1;
    case 't': values->tile_size = value; return 1;
    case 'q': values->quality = value;
              values->quant = value;     return 1;
    case 'l': values->layers = value;    return 1;
    case 'H': values->host = value;      return 1;
    case 'p': values->port_str = value;  return 1;
    case 'R': values->rate = value;      return 1;
    default: return 0;
  }
}

static int finalproj_parse_command(int argc, char** argv,
                                   const char* command,
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
  if (fread(magic, 1u, 2u, file) != 2u) { fclose(file); return "image_recon.pgm or image_recon.ppm"; }
  fclose(file);
  if (strcmp(magic, "P5") == 0) return FINALPROJ_RECON_GRAY_PATH;
  if (strcmp(magic, "P6") == 0) return FINALPROJ_RECON_RGB_PATH;
  return "image_recon.pgm or image_recon.ppm";
}

/* ========================================================================= */
/*  bit encode / decode / codec / info                                       */
/* ========================================================================= */

static int finalproj_bit_encode(int argc, char** argv) {
  const cag_option options[] = {
      {'i', "i", "input",  "FILE", "input PGM or PPM image"},
      {'o', "o", "output", "FILE", "output bitstream file (default: image.bit)"},
      {'q', "q", "quant",  "N",    "positive quantization step"},
      {'h', "h", "help",   NULL,   "show this help"}};
  finalproj_cli_values values;
  const char* output_path;
  double bitrate;
  int q = 0;
  int parse_result = finalproj_parse_command(
      argc, argv, "bit encode", options, CAG_ARRAY_SIZE(options), &values);
  if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
  if (values.input == NULL ||
      !finalproj_parse_int_range(values.quant, 1, 1000000, &q)) {
    fprintf(stderr, "error: --input and positive --quant are required\n");
    return 1;
  }
  output_path = values.output != NULL ? values.output : FINALPROJ_BITSTREAM_PATH;
  bitrate = imageEncoder(values.input, q, output_path);
  if (bitrate < 0.0) {
    fprintf(stderr, "error: encode command failed\n");
    return 1;
  }
  printf("wrote %s\n", output_path);
  printf("Bitrate %.6f\n", bitrate);
  return 0;
}

static int finalproj_bit_decode(int argc, char** argv) {
  const cag_option options[] = {
      {'b', "b", "bitstream", "FILE", "input basic codec bitstream"},
      {'q', "q", "quant",     "N",    "positive quantization step"},
      {'r', "r", "original",  "FILE", "original PGM or PPM image"},
      {'h', "h", "help",      NULL,   "show this help"}};
  finalproj_cli_values values;
  double psnr;
  int q = 0;
  int parse_result = finalproj_parse_command(
      argc, argv, "bit decode", options, CAG_ARRAY_SIZE(options), &values);
  if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
  if (values.bitstream == NULL || values.original == NULL ||
      !finalproj_parse_int_range(values.quant, 1, 1000000, &q)) {
    fprintf(stderr, "error: --bitstream, --original, and positive --quant are required\n");
    return 1;
  }
  psnr = imageDecoder(values.bitstream, q, values.original);
  if (psnr < 0.0) {
    fprintf(stderr, "error: decode command failed\n");
    return 1;
  }
  printf("PSNR %.6f\n", psnr);
  printf("wrote %s\n", finalproj_expected_reconstruction_path(values.original));
  return 0;
}

static int finalproj_bit_codec(int argc, char** argv) {
  const cag_option options[] = {
      {'i', "i", "input", "FILE", "input PGM or PPM image"},
      {'q', "q", "quant", "N",    "positive quantization step"},
      {'h', "h", "help",  NULL,   "show this help"}};
  finalproj_cli_values values;
  double bitrate, psnr;
  int q = 0;
  int parse_result = finalproj_parse_command(
      argc, argv, "bit codec", options, CAG_ARRAY_SIZE(options), &values);
  if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
  if (values.input == NULL ||
      !finalproj_parse_int_range(values.quant, 1, 1000000, &q)) {
    fprintf(stderr, "error: --input and positive --quant are required\n");
    return 1;
  }
  bitrate = imageEncoder(values.input, q, FINALPROJ_BITSTREAM_PATH);
  psnr = bitrate >= 0.0
             ? imageDecoder(FINALPROJ_BITSTREAM_PATH, q, values.input)
             : -1.0;
  if (bitrate < 0.0 || psnr < 0.0) {
    fprintf(stderr, "error: codec roundtrip failed\n");
    return 1;
  }
  printf("Bitrate %.6f\n", bitrate);
  printf("PSNR %.6f\n", psnr);
  return 0;
}

static int finalproj_bit_info(int argc, char** argv) {
  const cag_option options[] = {
      {'b', "b", "bitstream", "FILE", "input basic codec bitstream"},
      {'h', "h", "help",      NULL,   "show this help"}};
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

/* ========================================================================= */
/*  bit send / receive                                                       */
/* ========================================================================= */

static int finalproj_bit_send(int argc, char** argv) {
  const cag_option options[] = {
      {'i', "i", "input", "FILE",       "input PGM or PPM image"},
      {'H', NULL, "host",  "HOST",      "destination hostname or IP address"},
      {'p', NULL, "port",  "PORT",      "destination port"},
      {'q', "q", "quant",  "N",         "positive quantization step"},
      {'R', NULL, "rate",  "BYTES/SEC", "optional send rate limit (0 = unlimited)"},
      {'h', "h", "help",   NULL,        "show this help"}};
  finalproj_cli_values values;
  int q = 0, port = 0;
  uint32_t rate_limit = 0u;
  int parse_result = finalproj_parse_command(
      argc, argv, "bit send", options, CAG_ARRAY_SIZE(options), &values);
  if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
  if (values.input == NULL || values.host == NULL || values.port_str == NULL ||
      !finalproj_parse_int_range(values.quant, 1, 1000000, &q)) {
    fprintf(stderr, "error: --input, --host, --port, and positive --quant are required\n");
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

static int finalproj_bit_receive(int argc, char** argv) {
  const cag_option options[] = {
      {'p', NULL, "port",     "PORT", "listen port"},
      {'o', "o", "output",   "FILE", "output PGM or PPM image"},
      {'q', "q", "quant",    "N",    "positive quantization step (must match sender)"},
      {'r', "r", "original", "FILE", "optional original image for PSNR comparison"},
      {'h', "h", "help",     NULL,   "show this help"}};
  finalproj_cli_values values;
  int q = 0, port = 0;
  int parse_result = finalproj_parse_command(
      argc, argv, "bit receive", options, CAG_ARRAY_SIZE(options), &values);
  if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
  if (values.port_str == NULL || values.output == NULL ||
      !finalproj_parse_int_range(values.quant, 1, 1000000, &q)) {
    fprintf(stderr, "error: --port, --output, and positive --quant are required\n");
    return 1;
  }
  if (!finalproj_parse_int_range(values.port_str, 1, 65535, &port)) {
    fprintf(stderr, "error: --port must be in [1, 65535]\n");
    return 1;
  }
  if (!networkReceive(port, values.output, q, values.original)) {
    fprintf(stderr, "error: receive command failed\n");
    return 1;
  }
  return 0;
}

/* ========================================================================= */
/*  j2k write / write-tiled / read / info                                    */
/* ========================================================================= */

static int finalproj_j2k_write(int argc, char** argv) {
  const cag_option options[] = {
      {'i', "i", "input",   "FILE", "input PGM or PPM image"},
      {'o', "o", "output",  "FILE", "output .j2k or .jp2 file"},
      {'q', "q", "quality", "Q",    "JPEG 2000 quality; -1 is reversible"},
      {'h', "h", "help",    NULL,   "show this help"}};
  finalproj_cli_values values;
  finalproj_j2k_container container;
  int quality = -1, ok = 0;
  int parse_result = finalproj_parse_command(
      argc, argv, "j2k write", options, CAG_ARRAY_SIZE(options), &values);
  if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
  if (values.input == NULL || values.output == NULL ||
      !finalproj_parse_quality(values.quality, &quality)) {
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
  if (ok) printf("wrote %s\n", values.output);
  else    fprintf(stderr, "error: j2k write command failed\n");
  return ok ? 0 : 1;
}

static int finalproj_j2k_write_tiled(int argc, char** argv) {
  const cag_option options[] = {
      {'i', "i", "input",     "FILE",    "input PGM or PPM image"},
      {'o', "o", "output",    "FILE",    "output .jp2 file"},
      {'t', NULL, "tile-size", "N|auto", "tile edge size or auto"},
      {'l', NULL, "layers",    "N",      "positive quality layer count"},
      {'h', "h", "help",      NULL,      "show this help"}};
  finalproj_cli_values values;
  int tile_size = 0, layers = 0, ok;
  int parse_result = finalproj_parse_command(
      argc, argv, "j2k write-tiled", options, CAG_ARRAY_SIZE(options), &values);
  if (parse_result <= 0) return parse_result < 0 ? 0 : 1;
  if (values.input == NULL || values.output == NULL ||
      values.tile_size == NULL ||
      !finalproj_parse_int_range(values.layers, 1, 1000000, &layers)) {
    fprintf(stderr, "error: --input, --output, --tile-size, and positive --layers are required\n");
    return 1;
  }
  if (finalproj_j2k_container_from_path(values.output) != finalproj_J2K_CONTAINER_JP2) {
    fprintf(stderr, "error: --output must end in .jp2\n");
    return 1;
  }
  if (strcmp(values.tile_size, "auto") != 0 &&
      !finalproj_parse_int_range(values.tile_size, 1, 1000000, &tile_size)) {
    fprintf(stderr, "error: --tile-size must be auto or a positive integer\n");
    return 1;
  }
  ok = imageWriteJP2Tiled(values.input, values.output, tile_size, layers);
  if (ok) printf("wrote %s\n", values.output);
  else    fprintf(stderr, "error: j2k write-tiled command failed\n");
  return ok ? 0 : 1;
}

static int finalproj_j2k_read(int argc, char** argv) {
  const cag_option options[] = {
      {'i', "i", "input",  "FILE", "input .j2k or .jp2 file"},
      {'o', "o", "output", "FILE", "output PGM or PPM image"},
      {'l', NULL, "layers", "N",   "optional quality layers to decode"},
      {'h', "h", "help",   NULL,   "show this help"}};
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
  if (ok) printf("wrote %s\n", values.output);
  else    fprintf(stderr, "error: j2k read command failed\n");
  return ok ? 0 : 1;
}

static int finalproj_j2k_info(int argc, char** argv) {
  const cag_option options[] = {
      {'i', "i", "input", "FILE", "input .j2k or .jp2 file"},
      {'h', "h", "help",  NULL,   "show this help"}};
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

/* ========================================================================= */
/*  Top-level dispatch                                                       */
/* ========================================================================= */

static int finalproj_dispatch_bit(int argc, char** argv) {
  if (argc < 1) goto unknown;
  if (strcmp(argv[0], "encode") == 0)  return finalproj_bit_encode(argc, argv);
  if (strcmp(argv[0], "decode") == 0)  return finalproj_bit_decode(argc, argv);
  if (strcmp(argv[0], "codec") == 0)   return finalproj_bit_codec(argc, argv);
  if (strcmp(argv[0], "send") == 0)    return finalproj_bit_send(argc, argv);
  if (strcmp(argv[0], "receive") == 0) return finalproj_bit_receive(argc, argv);
  if (strcmp(argv[0], "info") == 0)    return finalproj_bit_info(argc, argv);
unknown:
  fprintf(stderr, "error: unknown bit subcommand: %s\n",
          argc >= 1 ? argv[0] : "(none)");
  return 1;
}

static int finalproj_dispatch_j2k(int argc, char** argv) {
  if (argc < 1) goto unknown;
  if (strcmp(argv[0], "write") == 0)       return finalproj_j2k_write(argc, argv);
  if (strcmp(argv[0], "write-tiled") == 0) return finalproj_j2k_write_tiled(argc, argv);
  if (strcmp(argv[0], "read") == 0)        return finalproj_j2k_read(argc, argv);
  if (strcmp(argv[0], "info") == 0)        return finalproj_j2k_info(argc, argv);
unknown:
  fprintf(stderr, "error: unknown j2k subcommand: %s\n",
          argc >= 1 ? argv[0] : "(none)");
  return 1;
}

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

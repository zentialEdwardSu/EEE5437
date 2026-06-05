/**
 * @file lib_codec_test_dht.c
 * @brief JPEG DHT (Define Huffman Table) marker parser utility.
 *
 * Reads a JPEG file and prints the Huffman tables found in DHT markers.
 * This demonstrates how JPEG uses fixed Huffman tables (Annex K of ITU-T T.81),
 * providing the reference methodology for the DICW codec's fixed-table
 * approach.
 *
 * Usage: build/bin/lib_codec_test_dht.exe <file.jpg>
 *
 * References:
 *   ITU-T T.81 (JPEG) Annex B — Marker segments
 *   ITU-T T.81 Annex C — Huffman table specification
 *   ITU-T T.81 Annex K — Standard Huffman tables
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── JPEG marker constants ─────────────────────────────────────────────── */

#define JPEG_MARKER_PREFIX 0xFFu
#define JPEG_MARKER_SOI 0xD8u     /* Start of Image                  */
#define JPEG_MARKER_EOI 0xD9u     /* End of Image                    */
#define JPEG_MARKER_SOS 0xDAu     /* Start of Scan (stop parsing)    */
#define JPEG_MARKER_DHT 0xC4u     /* Define Huffman Table            */
#define JPEG_MARKER_DQT 0xDBu     /* Define Quantization Table       */
#define JPEG_MARKER_SOF0 0xC0u    /* Start of Frame (baseline DCT)   */
#define JPEG_MARKER_APP0 0xE0u    /* JFIF APP0                       */
#define JPEG_MARKER_APPn_LO 0xE0u /* APPn range start                */
#define JPEG_MARKER_APPn_HI 0xEFu /* APPn range end                  */
#define JPEG_MARKER_COM 0xFEu     /* Comment                         */
#define JPEG_MARKER_DRI 0xDDu     /* Define Restart Interval         */
#define JPEG_MARKER_RST0 0xD0u    /* Restart marker 0                */
#define JPEG_MARKER_RST7 0xD7u    /* Restart marker 7                */

/* ─── Helper functions ──────────────────────────────────────────────────── */

/** Read a big-endian uint16_t from file.  Returns 0 on failure. */
static int read_u16_be(FILE* f, uint16_t* out) {
  unsigned char buf[2];
  if (fread(buf, 1u, 2u, f) != 2u) return 0;
  *out = (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
  return 1;
}

/** Read a single byte from file.  Returns 0 on failure. */
static int read_u8(FILE* f, unsigned char* out) {
  return fread(out, 1u, 1u, f) == 1u;
}

/* ─── DHT parser ────────────────────────────────────────────────────────── */

/**
 * @brief Parse and print a single Huffman table from a DHT segment.
 *
 * DHT segment structure (ITU-T T.81 § B.2.4.2):
 *   - 1 byte:  Tc (upper nibble) | Th (lower nibble)
 *       Tc = 0 → DC table,  Tc = 1 → AC table
 *       Th = table destination ID (0..3)
 *   - 16 bytes: L_i — number of Huffman codes of length i (i = 1..16)
 *   - sum(L_i) bytes: V_ij — symbol values, grouped by increasing code length
 *
 * @param f     Open file positioned at the start of DHT data (past marker +
 * length).
 * @param length  Length field value (covers the entire segment including the
 *                2-byte length field itself, so data bytes = length − 2).
 * @return 1 on success, 0 on parse error.
 */
static int parse_dht(FILE* f, uint16_t length) {
  unsigned char tc_th;
  unsigned char counts[16];
  int total_symbols;
  unsigned char symbols[256];
  int i, s;
  const char* table_class;

  /* length includes the 2-byte length field; actual payload = length - 2 */
  size_t payload = (size_t)(length - 2u);

  if (payload < 17u) {
    fprintf(stderr, "DHT: segment too short (%zu bytes, need ≥ 17)\n", payload);
    return 0;
  }

  /* --- Tc | Th byte --- */
  if (!read_u8(f, &tc_th)) return 0;
  --payload;

  {
    unsigned int tc = (tc_th >> 4) & 0xFu;
    unsigned int th = tc_th & 0xFu;

    if (tc > 1u) {
      fprintf(stderr, "DHT: invalid table class %u (expected 0 or 1)\n", tc);
      return 0;
    }
    table_class = (tc == 0u) ? "DC" : "AC";

    printf("┌─────────────────────────────────────────────┐\n");
    printf("│  Huffman Table: %s  (destination ID = %u)     │\n", table_class,
           th);
    printf("├─────────────────────────────────────────────┤\n");
  }

  /* --- 16 code counts L_1 … L_16 --- */
  total_symbols = 0;
  for (i = 0; i < 16; ++i) {
    if (!read_u8(f, &counts[i])) return 0;
    total_symbols += (int)counts[i];
  }
  payload -= 16u;

  if (total_symbols > 256 || (size_t)total_symbols > payload) {
    fprintf(stderr, "DHT: symbol count %d exceeds remaining payload %zu\n",
            total_symbols, payload);
    return 0;
  }

  /* --- Symbol values V_ij --- */
  for (s = 0; s < total_symbols; ++s) {
    if (!read_u8(f, &symbols[s])) return 0;
  }
  payload -= (size_t)total_symbols;

  /* Skip any remaining payload bytes (should be none, but be tolerant) */
  if (payload > 0u) {
    fseek(f, (long)payload, SEEK_CUR);
  }

  /* --- Print canonical codewords --- */
  {
    unsigned int code = 0u;
    int idx = 0;

    printf("│  %-4s  %-5s  %-12s  %s\n", "Len", "Count", "Codeword", "Symbols");
    printf("│  ────  ─────  ────────────  ───────\n");

    for (i = 0; i < 16; ++i) {
      int len = i + 1;
      if (counts[i] > 0u) {
        int j;
        printf("│  %-4d  %-5d  ", len, (int)counts[i]);

        /* Print first codeword as binary */
        {
          char bits[17];
          int b;
          for (b = 0; b < len; ++b)
            bits[b] = ((code >> (len - 1 - b)) & 1u) ? '1' : '0';
          bits[len] = '\0';
          printf("%-12s  ", bits);
        }

        /* Print symbol values (as hex) */
        for (j = 0; j < (int)counts[i]; ++j) {
          printf("%02X", symbols[idx + j]);
          if (j + 1 < (int)counts[i]) printf(" ");
        }
        printf("\n");

        idx += (int)counts[i];
      }
      code = (code + (unsigned int)counts[i]) << 1u;
    }
    printf("└─────────────────────────────────────────────┘\n\n");
  }

  return 1;
}

/* ─── Main JPEG scanner ─────────────────────────────────────────────────── */

int main(int argc, char** argv) {
  FILE* f;
  int table_count = 0;

  if (argc < 2) {
    /* No argument provided — skip gracefully (this is a manual utility,
     * not an automated test). */
    printf("lib_codec_test_dht: SKIP — no input file specified.\n");
    printf("Usage: %s <file.jpg>\n", argv[0]);
    return 0;
  }

  f = fopen(argv[1], "rb");
  if (f == NULL) {
    fprintf(stderr, "Error: cannot open '%s'\n", argv[1]);
    return 1;
  }

  /* Verify SOI marker (FF D8) */
  {
    unsigned char b0, b1;
    if (!read_u8(f, &b0) || !read_u8(f, &b1) || b0 != JPEG_MARKER_PREFIX ||
        b1 != JPEG_MARKER_SOI) {
      fprintf(stderr, "Error: not a JPEG file (missing SOI marker)\n");
      fclose(f);
      return 1;
    }
  }

  printf("JPEG Huffman Table Extractor\n");
  printf("File: %s\n\n", argv[1]);

  /* Scan for markers */
  for (;;) {
    unsigned char b0, b1;
    uint16_t length;

    /* Find next marker prefix (0xFF) */
    if (!read_u8(f, &b0)) break;

    /* Skip padding 0xFF bytes (ITU-T T.81 § B.1.1.2) */
    while (b0 == JPEG_MARKER_PREFIX) {
      if (!read_u8(f, &b1)) goto done;

      /* 0xFF 0x00 is a stuffed byte in entropy-coded data,
       * not a marker.  If we hit this, scan forward. */
      if (b1 == 0x00u) {
        if (!read_u8(f, &b0)) goto done;
        continue;
      }

      /* Valid marker: b1 is the marker code */
      if (b1 == JPEG_MARKER_SOS) {
        /* Start of Scan — entropy-coded data follows.
         * Stop parsing here. */
        printf("── Reached SOS marker (0x%02X%02X) — stopping. ──\n",
               JPEG_MARKER_PREFIX, JPEG_MARKER_SOS);
        goto done;
      }

      if (b1 == JPEG_MARKER_EOI) {
        printf("── Reached EOI marker. ──\n");
        goto done;
      }

      /* RST markers: no length field, skip */
      if (b1 >= JPEG_MARKER_RST0 && b1 <= JPEG_MARKER_RST7) {
        if (!read_u8(f, &b0)) goto done;
        continue;
      }

      /* Read segment length (big-endian) */
      if (!read_u16_be(f, &length)) goto done;

      if (b1 == JPEG_MARKER_DHT) {
        /* Parse this DHT segment */
        table_count++;
        if (!parse_dht(f, length))
          fprintf(stderr, "Warning: failed to parse DHT #%d\n", table_count);
      } else {
        /* Skip this segment */
        long skip = (long)length - 2L; /* length includes itself */
        if (skip > 0) fseek(f, skip, SEEK_CUR);
      }

      if (!read_u8(f, &b0)) goto done;
    }
  }

done:
  if (table_count == 0)
    printf(
        "No DHT markers found.  This JPEG may use optimized "
        "(non-standard) tables embedded via a proprietary encoder.\n");
  else
    printf("── %d Huffman table(s) found. ──\n", table_count);

  fclose(f);
  return 0;
}

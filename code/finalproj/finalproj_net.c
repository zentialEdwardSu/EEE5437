/**
 * @file finalproj_net.c
 * @brief Implements network send/receive using lib_net and the basic codec.
 *
 * The sender encodes an image, serializes it to a DICW v3 buffer, and
 * transmits it over TCP with an optional rate limit.
 *
 * The receiver listens on a TCP port, deserializes the received buffer, and
 * decodes it progressively — first by resolution scalability (0..levels)
 * then by quality scalability (1..max_bitplanes at full resolution).  PSNR
 * is printed at each step when an original reference image is provided.
 */

#include "finalproj/finalproj_net.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec/basic_codec.h"
#include "codec/basic_file.h"
#include "codec/metrics.h"
#include "errors/errors.h"
#include "finalproj/finalproj_codec.h"
#include "image_u8/image_u8.h"
#include "net/net.h"
#include "net/net_platform.h"
#include "ppm/ppm.h"

/* -------------------------------------------------------------------------- */
/*  Internal helpers                                                          */
/* -------------------------------------------------------------------------- */

/** Polling receive loop: reads exactly `total` bytes into `buf`. */
static dic_status net_receive_all(net_control* net, uint8_t* buf,
                                  size_t total) {
  size_t received_total = 0u;
  int attempts = 0;
  const int max_attempts = 10000; /* ~100 s at 10 ms each */

  while (received_total < total) {
    size_t received = 0u;
    dic_status status = net_receive(net, buf + received_total,
                                    total - received_total, &received);
    if (status != DIC_STATUS_OK) return status;
    if (received > 0u) {
      received_total += received;
      attempts = 0;
    } else {
      attempts++;
      if (attempts >= max_attempts) return DIC_NET_RECEIVE_ERROR;
      net_platform_sleep_ms(10);
    }
  }
  return DIC_STATUS_OK;
}

/**
 * Computes PSNR between a decoded image and an original, downsampling
 * the original via box averaging when the dimensions differ (sub-full
 * resolution decode).
 */
static double compute_psnr_scaled(const dic_image_u8* original,
                                  const dic_image_u8* decoded) {
  int c, y, x;
  uint8_t* reference;
  double mse;

  if (original == NULL || decoded == NULL) return -1.0;

  if (original->width == decoded->width &&
      original->height == decoded->height) {
    return codec_metric_psnr_u8(
        original->data, decoded->data,
        dic_image_u8_sample_count(decoded->width, decoded->height,
                                  decoded->channels));
  }

  /* Box-average downsample original to match decoded dimensions. */
  {
    int scale_x = original->width / decoded->width;
    int scale_y = original->height / decoded->height;
    if (scale_x < 1 || scale_y < 1 ||
        original->width != decoded->width * scale_x ||
        original->height != decoded->height * scale_y) {
      return -1.0;
    }

    reference =
        (uint8_t*)malloc((size_t)decoded->width * (size_t)decoded->height *
                         (size_t)decoded->channels);
    if (reference == NULL) return -1.0;

    for (y = 0; y < decoded->height; ++y) {
      for (x = 0; x < decoded->width; ++x) {
        for (c = 0; c < decoded->channels; ++c) {
          int sum = 0;
          int sy, sx;
          for (sy = 0; sy < scale_y; ++sy) {
            for (sx = 0; sx < scale_x; ++sx) {
              int ox = x * scale_x + sx;
              int oy = y * scale_y + sy;
              sum += (int)original->data[((size_t)oy * (size_t)original->width +
                                          (size_t)ox) *
                                             (size_t)original->channels +
                                         (size_t)c];
            }
          }
          reference[((size_t)y * (size_t)decoded->width + (size_t)x) *
                        (size_t)decoded->channels +
                    (size_t)c] = (uint8_t)(sum / (scale_x * scale_y));
        }
      }
    }
  }

  mse = codec_metric_mse_u8(
      reference, decoded->data,
      dic_image_u8_sample_count(decoded->width, decoded->height,
                                decoded->channels));
  free(reference);

  if (mse <= 0.0) return 100.0; /* perfect match */
  return 10.0 * log10(255.0 * 255.0 / mse);
}

/**
 * @brief Return the maximum number of bitplanes across all resolutions
 *        and channels in an encoded image.
 */
static int encoded_max_bitplanes(const codec_basic_encoded_image* encoded) {
  int max_bp = 0;
  int ch, res;
  for (ch = 0; ch < encoded->channels; ++ch) {
    const codec_basic_channel_stream* stream = encoded->channel_streams + ch;
    for (res = 0; res < stream->num_resolutions; ++res) {
      if (stream->resolutions[res].num_bitplanes > max_bp)
        max_bp = stream->resolutions[res].num_bitplanes;
    }
  }
  return max_bp;
}

/**
 * @brief Compute output dimensions for a given max_resolution.
 */
static void output_dimensions(const codec_basic_encoded_image* encoded,
                              int max_resolution, int* out_width,
                              int* out_height) {
  int scale = 1;
  int steps = encoded->levels - max_resolution;
  int lvl;

  /* Guard against integer overflow: 2^30 fits in int32; 2^31 overflows.
     Clamp iteration count so scale stays representable. */
  if (steps > 30) steps = 30;

  for (lvl = 0; lvl < steps; ++lvl) scale *= 2;
  *out_width = encoded->width / scale;
  *out_height = encoded->height / scale;
}

/* -------------------------------------------------------------------------- */
/*  Atomic PPM write helper */
/* -------------------------------------------------------------------------- */

/** Write a PPM file atomically: write to a temp file, then rename.
 *  Prevents a polling viewer from reading a partially-written file. */
static dic_status write_ppm_atomic(const char* path,
                                   const dic_image_u8* image) {
  char tmp_path[1024];
  int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
  if (n < 0 || (size_t)n >= sizeof(tmp_path)) return DIC_STATUS_IO_ERROR;

  dic_status s = dic_ppm_write(tmp_path, image);
  if (s != DIC_STATUS_OK) {
    remove(tmp_path);
    return s;
  }

  /* Atomic replace: remove old, then rename.  Brief window where
     file is absent is handled by the viewer (shows black). */
  remove(path);
  if (rename(tmp_path, path) != 0) {
    remove(tmp_path);
    return DIC_STATUS_IO_ERROR;
  }
  return DIC_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

int networkSend(const char* inputFile, const char* host, int port, int quant,
                uint32_t rateLimit) {
  dic_image_u8 image;
  codec_basic_encoded_image encoded;
  dic_status status;
  net_config config;
  net_control* net = NULL;
  uint8_t* buffer = NULL;
  size_t buffer_size = 0u;
  size_t sent = 0u;
  uint8_t header[4];
  int ok = 0;

  dic_image_u8_init(&image);
  codec_basic_encoded_init(&encoded);

  /* 1. Read input image */
  status = dic_ppm_read(inputFile, &image);
  if (status != DIC_STATUS_OK) {
    fprintf(stderr, "error: failed to read input image: %s\n",
            dic_status_message(status));
    goto cleanup;
  }

  /* 2. Encode */
  status = codec_basic_encode_image(image.data, image.width, image.height,
                                    image.channels, FINALPROJ_LEVELS, quant,
                                    &encoded);
  if (status != DIC_STATUS_OK) {
    fprintf(stderr, "error: encode failed: %s\n", dic_status_message(status));
    goto cleanup;
  }

  /* 3. Serialize to buffer */
  status = codec_basic_serialize(&encoded, &buffer, &buffer_size);
  if (status != DIC_STATUS_OK) {
    fprintf(stderr, "error: serialize failed: %s\n",
            dic_status_message(status));
    goto cleanup;
  }

  /* 4. Open TCP connection */
  net_config_init(&config);
  config.transport = net_TRANSPORT_TCP;
  config.host = host;
  config.peer_port = (uint16_t)port;
  config.buffer_capacity = 256u * 1024u;
  if (rateLimit > 0u) config.bytes_per_second = rateLimit;

  status = net_control_open(&net, &config);
  if (status != DIC_STATUS_OK) {
    fprintf(stderr, "error: failed to connect to %s:%d: %s\n", host, port,
            dic_status_message(status));
    goto cleanup;
  }

  if (rateLimit > 0u)
    printf("Sending at %u bytes/sec...\n", rateLimit);
  else
    printf("Sending...\n");

  /* 5. Send 4-byte payload size header (little-endian) */
  header[0] = (uint8_t)(buffer_size & 0xffu);
  header[1] = (uint8_t)((buffer_size >> 8) & 0xffu);
  header[2] = (uint8_t)((buffer_size >> 16) & 0xffu);
  header[3] = (uint8_t)((buffer_size >> 24) & 0xffu);
  status = net_send(net, header, sizeof(header), &sent);
  if (status != DIC_STATUS_OK || sent != sizeof(header)) {
    fprintf(stderr, "error: failed to send header: %s (%s)\n",
            dic_status_message(DIC_NET_SEND_ERROR), dic_status_message(status));
    goto cleanup;
  }

  /* 6. Send payload */
  status = net_send(net, buffer, buffer_size, &sent);
  if (status != DIC_STATUS_OK || sent != buffer_size) {
    fprintf(stderr, "error: failed to send payload: %s (%s)\n",
            dic_status_message(DIC_NET_SEND_ERROR), dic_status_message(status));
    goto cleanup;
  }

  printf("Sent %zu bytes (%zux%zu, %d channel(s), quant=%d)\n", buffer_size,
         (size_t)image.width, (size_t)image.height, image.channels, quant);
  ok = 1;

cleanup:
  if (net != NULL) net_control_close(net);
  free(buffer);
  codec_basic_encoded_free(&encoded);
  dic_image_u8_free(&image);
  return ok;
}

int networkReceive(int port, const char* outputFile, int quant,
                   const char* originalFile) {
  net_config config;
  net_control* net = NULL;
  dic_status status;
  uint8_t header[4];
  uint32_t payload_size;
  uint8_t* buffer = NULL;
  codec_basic_encoded_image encoded;
  dic_image_u8 original;
  dic_image_u8 decoded;
  int has_original = 0;
  int ok = 0;
  int res, bp, max_bp;
  int out_w, out_h;

  dic_image_u8_init(&original);
  dic_image_u8_init(&decoded);
  codec_basic_encoded_init(&encoded);

  /* 1. Listen on TCP port */
  net_config_init(&config);
  config.transport = net_TRANSPORT_TCP;
  config.bind_port = (uint16_t)port;
  config.peer_port = 0u; /* server mode */
  config.buffer_capacity = 256u * 1024u;

  status = net_control_open(&net, &config);
  if (status != DIC_STATUS_OK) {
    fprintf(stderr, "error: failed to listen on port %d: %s\n", port,
            dic_status_message(status));
    goto cleanup;
  }

  printf("Listening on port %d...\n", (int)net_control_port(net));

  /* 2. Receive 4-byte header */
  status = net_receive_all(net, header, sizeof(header));
  if (status != DIC_STATUS_OK) {
    fprintf(stderr, "error: failed to receive header: %s\n",
            dic_status_message(status));
    goto cleanup;
  }
  payload_size = (uint32_t)header[0] | ((uint32_t)header[1] << 8) |
                 ((uint32_t)header[2] << 16) | ((uint32_t)header[3] << 24);

  if (payload_size == 0u || payload_size > 256u * 1024u * 1024u) {
    fprintf(stderr, "error: invalid payload size: %u\n", payload_size);
    goto cleanup;
  }

  /* 3. Receive payload */
  buffer = (uint8_t*)malloc(payload_size);
  if (buffer == NULL) {
    fprintf(stderr, "error: memory allocation failed\n");
    goto cleanup;
  }

  status = net_receive_all(net, buffer, payload_size);
  if (status != DIC_STATUS_OK) {
    fprintf(stderr, "error: failed to receive payload: %s\n",
            dic_status_message(status));
    goto cleanup;
  }

  printf("Receiving encoded image... done (%u bytes)\n", payload_size);

  /* 4. Deserialize */
  status = codec_basic_deserialize(buffer, payload_size, &encoded);
  if (status != DIC_STATUS_OK) {
    fprintf(stderr, "error: deserialize failed: %s\n",
            dic_status_message(status));
    goto cleanup;
  }

  /* Validate quant_step */
  if (encoded.quant_step != quant) {
    fprintf(stderr,
            "warning: received quant_step=%d but expected %d; "
            "using received value\n",
            encoded.quant_step, quant);
    quant = encoded.quant_step;
  }

  printf("Image: %dx%d, %d channels, %d levels, quant=%d\n", encoded.width,
         encoded.height, encoded.channels, encoded.levels, encoded.quant_step);

  /* 5. Load original for PSNR if provided */
  if (originalFile != NULL) {
    status = dic_ppm_read(originalFile, &original);
    if (status == DIC_STATUS_OK) {
      has_original = 1;
    } else {
      fprintf(stderr, "warning: could not read original image '%s': %s\n",
              originalFile, dic_status_message(status));
    }
  }

  /* 6. Resolution scalability scan */
  printf("\n--- Resolution scalability ---\n");
  for (res = 0; res <= encoded.levels; ++res) {
    output_dimensions(&encoded, res, &out_w, &out_h);
    dic_image_u8_free(&decoded);

    status = codec_basic_decode_image(&encoded, res, 0, &decoded);
    if (status != DIC_STATUS_OK) {
      printf("Resolution %d (%dx%d):  decode failed\n", res, out_w, out_h);
      continue;
    }

    if (has_original) {
      double psnr = compute_psnr_scaled(&original, &decoded);
      printf("Resolution %d (%dx%d):      PSNR = %.1f dB\n", res, out_w, out_h,
             psnr);
    } else {
      printf("Resolution %d (%dx%d):      N/A\n", res, out_w, out_h);
    }
  }

  /* 7. Quality scalability scan (full resolution) */
  max_bp = encoded_max_bitplanes(&encoded);
  printf("\n--- Quality scalability (full resolution) ---\n");
  for (bp = 1; bp <= max_bp; ++bp) {
    dic_image_u8_free(&decoded);

    status = codec_basic_decode_image(&encoded, encoded.levels, bp, &decoded);
    if (status != DIC_STATUS_OK) {
      printf("Bitplane %2d/%-2d:            decode failed\n", bp, max_bp);
      continue;
    }

    if (has_original) {
      double psnr = codec_metric_psnr_u8(
          original.data, decoded.data,
          dic_image_u8_sample_count(decoded.width, decoded.height,
                                    decoded.channels));
      printf("Bitplane %2d/%-2d:            PSNR = %.1f dB\n", bp, max_bp,
             psnr);
    } else {
      printf("Bitplane %2d/%-2d:            N/A\n", bp, max_bp);
    }

    /* Write intermediate PPM so a polling viewer can show
       progressive quality improvement as more bitplanes arrive. */
    status = write_ppm_atomic(outputFile, &decoded);
    if (status != DIC_STATUS_OK) {
      fprintf(stderr, "warning: intermediate PPM write failed: %s\n",
              dic_status_message(status));
    }
  }

  /* 8. Final full decode and save */
  dic_image_u8_free(&decoded);
  status = codec_basic_decode_image(&encoded, encoded.levels, 0, &decoded);
  if (status != DIC_STATUS_OK) {
    fprintf(stderr, "error: final decode failed: %s\n",
            dic_status_message(status));
    goto cleanup;
  }

  status = write_ppm_atomic(outputFile, &decoded);
  if (status != DIC_STATUS_OK) {
    fprintf(stderr, "error: failed to write output image: %s\n",
            dic_status_message(status));
    goto cleanup;
  }

  if (has_original) {
    double final_psnr = codec_metric_psnr_u8(
        original.data, decoded.data,
        dic_image_u8_sample_count(decoded.width, decoded.height,
                                  decoded.channels));
    printf("\nSaved reconstructed image to %s (PSNR = %.1f dB)\n", outputFile,
           final_psnr);
  } else {
    printf("\nSaved reconstructed image to %s\n", outputFile);
  }

  ok = 1;

cleanup:
  if (net != NULL) net_control_close(net);
  free(buffer);
  codec_basic_encoded_free(&encoded);
  dic_image_u8_free(&decoded);
  dic_image_u8_free(&original);
  return ok;
}

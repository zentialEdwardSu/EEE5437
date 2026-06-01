#pragma once
/**
 * @file dic_metrics.h
 * @brief Image quality and rate metrics for codec experiments.
 */

#include <stddef.h>
#include <stdint.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Computes mean squared error between two 8-bit sample arrays.
 * @param a First sample array.
 * @param b Second sample array.
 * @param sample_count Number of samples to compare.
 * @return MSE, or -1.0 for invalid input.
 */
double dic_metric_mse_u8(const uint8_t *a, const uint8_t *b, size_t sample_count);

/**
 * @brief Computes PSNR in dB between two 8-bit sample arrays.
 * @param a First sample array.
 * @param b Second sample array.
 * @param sample_count Number of samples to compare.
 * @return PSNR in dB, INFINITY for identical arrays, or -1.0 for invalid input.
 */
double dic_metric_psnr_u8(const uint8_t *a, const uint8_t *b, size_t sample_count);

/**
 * @brief Computes bitrate in bits per pixel.
 * @param encoded_bits Encoded bitstream size in bits.
 * @param width Image width.
 * @param height Image height.
 * @return Bits per pixel, or -1.0 for invalid dimensions.
 */
double dic_metric_bitrate(size_t encoded_bits, int width, int height);

#ifdef __cplusplus
}
#endif

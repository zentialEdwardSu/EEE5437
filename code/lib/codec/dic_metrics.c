/**
 * @file dic_metrics.c
 * @brief Implements MSE, PSNR, and bitrate metrics for codec output.
 */

#include "codec/dic_metrics.h"

#include <math.h>

double dic_metric_mse_u8(const uint8_t *a, const uint8_t *b, size_t sample_count)
{
    double sum = 0.0;
    size_t i;

    if ((a == NULL || b == NULL) && sample_count > 0u)
        return -1.0;
    if (sample_count == 0u)
        return 0.0;

    for (i = 0; i < sample_count; ++i)
    {
        double delta = (double)a[i] - (double)b[i];
        sum += delta * delta;
    }

    return sum / (double)sample_count;
}

double dic_metric_psnr_u8(const uint8_t *a, const uint8_t *b, size_t sample_count)
{
    double mse = dic_metric_mse_u8(a, b, sample_count);

    if (mse < 0.0)
        return -1.0;
    if (mse == 0.0)
        return INFINITY;

    return 10.0 * log10((255.0 * 255.0) / mse);
}

double dic_metric_bitrate(size_t encoded_bits, int width, int height)
{
    if (width <= 0 || height <= 0)
        return -1.0;

    return (double)encoded_bits / ((double)width * (double)height);
}

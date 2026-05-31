/**
 * @file j2k_ict.c
 * @brief Implements the irreversible multiple component transform from T.800 Annex G.
 *
 * The forward and inverse routines operate in-place on interleaved double RGB
 * samples after level shift. The forward path uses the JPEG 2000 ICT
 * coefficients from Annex G. The inverse path solves the same fixed transform
 * matrix, avoiding loss from composing two separately rounded coefficient sets.
 */

#include "j2k/j2k_ict.h"
#include "j2k/j2k_debug.h"

#define j2k_ICT_Y_R 0.299
#define j2k_ICT_Y_G 0.587
#define j2k_ICT_Y_B 0.114
#define j2k_ICT_CB_R -0.16875
#define j2k_ICT_CB_G -0.33126
#define j2k_ICT_CB_B 0.5
#define j2k_ICT_CR_R 0.5
#define j2k_ICT_CR_G -0.41869
#define j2k_ICT_CR_B -0.08131

/**
 * @brief Compute the determinant of a 3 by 3 matrix in row-major order.
 */
static double j2k_ict_det3(
    double a00,
    double a01,
    double a02,
    double a10,
    double a11,
    double a12,
    double a20,
    double a21,
    double a22
)
{
    j2k_DEBUG_ENTER();
    return a00 * (a11 * a22 - a12 * a21)
        - a01 * (a10 * a22 - a12 * a20)
        + a02 * (a10 * a21 - a11 * a20);
}

/**
 * @brief Solve the fixed ICT matrix equation for one RGB sample triple.
 */
static void j2k_ict_inverse_sample(double y, double cb, double cr, double *r, double *g, double *b)
{
    j2k_DEBUG_ENTER();
    const double determinant = j2k_ict_det3(
        j2k_ICT_Y_R,
        j2k_ICT_Y_G,
        j2k_ICT_Y_B,
        j2k_ICT_CB_R,
        j2k_ICT_CB_G,
        j2k_ICT_CB_B,
        j2k_ICT_CR_R,
        j2k_ICT_CR_G,
        j2k_ICT_CR_B
    );

    *r = j2k_ict_det3(
        y,
        j2k_ICT_Y_G,
        j2k_ICT_Y_B,
        cb,
        j2k_ICT_CB_G,
        j2k_ICT_CB_B,
        cr,
        j2k_ICT_CR_G,
        j2k_ICT_CR_B
    ) / determinant;
    *g = j2k_ict_det3(
        j2k_ICT_Y_R,
        y,
        j2k_ICT_Y_B,
        j2k_ICT_CB_R,
        cb,
        j2k_ICT_CB_B,
        j2k_ICT_CR_R,
        cr,
        j2k_ICT_CR_B
    ) / determinant;
    *b = j2k_ict_det3(
        j2k_ICT_Y_R,
        j2k_ICT_Y_G,
        y,
        j2k_ICT_CB_R,
        j2k_ICT_CB_G,
        cb,
        j2k_ICT_CR_R,
        j2k_ICT_CR_G,
        cr
    ) / determinant;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex G.2.2, ICT maps RGB to Y, Cb, Cr. */
dic_status j2k_ict_forward(
    double *samples,
    size_t sample_count
)
{
    j2k_DEBUG_ENTER();
    size_t sample;

    if (samples == NULL || sample_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (sample = 0u; sample < sample_count; ++sample)
    {
        double r = samples[sample * 3u + 0u];
        double g = samples[sample * 3u + 1u];
        double b = samples[sample * 3u + 2u];

        samples[sample * 3u + 0u] = j2k_ICT_Y_R * r + j2k_ICT_Y_G * g + j2k_ICT_Y_B * b;
        samples[sample * 3u + 1u] = j2k_ICT_CB_R * r + j2k_ICT_CB_G * g + j2k_ICT_CB_B * b;
        samples[sample * 3u + 2u] = j2k_ICT_CR_R * r + j2k_ICT_CR_G * g + j2k_ICT_CR_B * b;
    }

    return DIC_STATUS_OK;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex G.2.2, inverse ICT reconstructs RGB from Y, Cb, Cr. */
dic_status j2k_ict_inverse(
    double *samples,
    size_t sample_count
)
{
    j2k_DEBUG_ENTER();
    size_t sample;

    if (samples == NULL || sample_count == 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (sample = 0u; sample < sample_count; ++sample)
    {
        double r;
        double g;
        double b;
        double y = samples[sample * 3u + 0u];
        double cb = samples[sample * 3u + 1u];
        double cr = samples[sample * 3u + 2u];

        j2k_ict_inverse_sample(y, cb, cr, &r, &g, &b);
        samples[sample * 3u + 0u] = r;
        samples[sample * 3u + 1u] = g;
        samples[sample * 3u + 2u] = b;
    }

    return DIC_STATUS_OK;
}

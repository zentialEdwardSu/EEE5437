# JPEG 2000 9/7 Quality Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a standard-compatible minimum JPEG 2000 irreversible encode/decode path using 9/7 DWT and `Q=1..100`, while preserving existing lossless behavior with `Q=-1`.

**Architecture:** Add focused wavelet, ICT, and quantization modules first, then extend codestream metadata and marker syntax, then wire the new mode through image encode/decode and CLI. Existing ROI and tiled APIs remain reversible-only in this phase.

**Tech Stack:** C11, CMake glob-registered modules under `code/lib`, existing `DIC_EXPECT` tests, JPEG 2000 T.800 Annex A/B/E/F/G syntax, existing EBCOT/MQ packet code.

---

## File Structure

- Create `code/lib/wavelet/dic_dwt97.h`: public 9/7 DWT API using `double` planes.
- Create `code/lib/wavelet/dic_dwt97.c`: Annex F irreversible 9/7 lifting implementation.
- Create `code/lib/j2k/j2k_ict.h`: public ICT API using interleaved `double` RGB triples.
- Create `code/lib/j2k/j2k_ict.c`: forward and inverse ICT.
- Create `code/lib/j2k/j2k_quant.h`: JPEG 2000 irreversible quantization helpers and SPqcd/SPqcc pack/unpack API.
- Create `code/lib/j2k/j2k_quant.c`: Q mapping, subband step calculation, quantize/dequantize helpers.
- Modify `code/lib/j2k/j2k_codestream.h`: extend `j2k_basic_params` with quantization step metadata.
- Modify `code/lib/j2k/j2k_codestream.c`: write irreversible QCD/QCC and COD transform value.
- Modify `code/lib/j2k/j2k_parse.c`: parse irreversible QCD/QCC metadata.
- Modify `code/lib/j2k/j2k_decode.c`: decode irreversible quantized coefficients through inverse 9/7 + inverse ICT.
- Modify `code/lib/j2k/j2k_image.h`: add `quality` parameter to existing raw J2K and JP2 write APIs.
- Modify `code/lib/j2k/j2k_image.c`: route `quality=-1` to existing path and `quality=1..100` to new 9/7 path.
- Modify `code/finalproj/finalproj_codec.h`: add quality parameters to J2K/JP2 write helper wrappers.
- Modify `code/finalproj/finalproj_codec.c`: pass `quality` to the J2K library.
- Modify `code/finalproj/main.c`: parse optional `[Q]` for `j2k-encode` and `jp2-encode`.
- Create `code/tests/lib_wavelet_test_dwt97.c`: wavelet tests.
- Create `code/tests/lib_j2k_test_quant.c`: quantization and SPqcd/SPqcc tests.
- Modify `code/tests/lib_j2k_test_codestream.c`: marker syntax tests for irreversible mode.
- Modify `code/tests/lib_j2k_test_parse.c`: parser tests for irreversible metadata.
- Modify `code/tests/lib_j2k_test_image.c`: `Q=-1`, `Q=90`, `Q=50`, invalid-Q tests.
- Modify `report.html`: document the completed 9/7/Q path after code and tests pass.

The CMake project uses `CONFIGURE_DEPENDS` recursive source globs, so new `.c` files under `code/lib/wavelet`, `code/lib/j2k`, and `code/tests` are picked up after the next configure/build pass.

---

### Task 1: Add 9/7 DWT Module

**Files:**
- Create: `code/lib/wavelet/dic_dwt97.h`
- Create: `code/lib/wavelet/dic_dwt97.c`
- Create: `code/tests/lib_wavelet_test_dwt97.c`

- [ ] **Step 1: Write the failing 9/7 wavelet test**

Create `code/tests/lib_wavelet_test_dwt97.c`:

```c
#include <math.h>
#include <stddef.h>

#include "test_helpers.h"
#include "wavelet/dic_dwt97.h"

static void dic_expect_close(double actual, double expected, double tolerance)
{
    DIC_EXPECT(fabs(actual - expected) <= tolerance);
}

int main(void)
{
    double plane[] = {
        12.0, 18.0, 25.0, 31.0, 44.0,
        15.0, 21.0, 29.0, 34.0, 48.0,
        19.0, 27.0, 36.0, 42.0, 55.0,
        24.0, 31.0, 43.0, 49.0, 63.0
    };
    double original[sizeof(plane) / sizeof(plane[0])];
    size_t i;

    for (i = 0u; i < sizeof(plane) / sizeof(plane[0]); ++i)
        original[i] = plane[i];

    DIC_EXPECT(dic_dwt97_forward_plane(plane, 5, 4, 2) == DIC_STATUS_OK);
    DIC_EXPECT(dic_dwt97_inverse_plane(plane, 5, 4, 2) == DIC_STATUS_OK);
    for (i = 0u; i < sizeof(plane) / sizeof(plane[0]); ++i)
        dic_expect_close(plane[i], original[i], 1.0e-6);

    DIC_EXPECT(dic_dwt97_forward_plane(plane, 5, 4, -1) == DIC_STATUS_INVALID_ARGUMENT);
    return 0;
}
```

- [ ] **Step 2: Run the failing test build**

Run:

```text
cmake --build build --target lib_wavelet_test_dwt97
```

Expected: build fails because `wavelet/dic_dwt97.h` does not exist.

- [ ] **Step 3: Add the public 9/7 header**

Create `code/lib/wavelet/dic_dwt97.h`:

```c
#pragma once

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Applies the JPEG 2000 irreversible 9/7 forward DWT to a row-major double plane. */
dic_status dic_dwt97_forward_plane(double *plane, int width, int height, int levels);

/** Applies the JPEG 2000 irreversible 9/7 inverse DWT to a row-major double plane. */
dic_status dic_dwt97_inverse_plane(double *plane, int width, int height, int levels);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Add the 9/7 implementation**

Create `code/lib/wavelet/dic_dwt97.c`:

```c
#include "wavelet/dic_dwt97.h"

#include <stdlib.h>
#include <string.h>

#include "wavelet/dic_dwt53.h"

enum
{
    dic_DWT97_MAX_LEVELS = 32
};

static const double dic_DWT97_ALPHA = -1.586134342059924;
static const double dic_DWT97_BETA = -0.052980118572961;
static const double dic_DWT97_GAMMA = 0.882911075530934;
static const double dic_DWT97_DELTA = 0.443506852043971;
static const double dic_DWT97_K = 1.1496043988602418;

static double dic_dwt97_symmetric(const double *values, int length, int index)
{
    if (length <= 0)
        return 0.0;
    while (index < 0 || index >= length)
    {
        if (index < 0)
            index = -index;
        if (index >= length)
            index = 2 * length - 2 - index;
    }
    return values[index];
}

static dic_status dic_dwt97_forward_1d(double *samples, int length, double *scratch)
{
    int i;
    int low_count;
    int high_count;
    double *low;
    double *high;

    if (samples == NULL || scratch == NULL || length < 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (length <= 1)
        return DIC_STATUS_OK;

    low_count = dic_dwt53_low_size(length);
    high_count = dic_dwt53_high_size(length);
    low = scratch;
    high = scratch + low_count;

    for (i = 0; i < low_count; ++i)
        low[i] = samples[i * 2];
    for (i = 0; i < high_count; ++i)
        high[i] = samples[i * 2 + 1];

    for (i = 0; i < high_count; ++i)
        high[i] += dic_DWT97_ALPHA * (dic_dwt97_symmetric(low, low_count, i) + dic_dwt97_symmetric(low, low_count, i + 1));
    for (i = 0; i < low_count; ++i)
        low[i] += dic_DWT97_BETA * (dic_dwt97_symmetric(high, high_count, i - 1) + dic_dwt97_symmetric(high, high_count, i));
    for (i = 0; i < high_count; ++i)
        high[i] += dic_DWT97_GAMMA * (dic_dwt97_symmetric(low, low_count, i) + dic_dwt97_symmetric(low, low_count, i + 1));
    for (i = 0; i < low_count; ++i)
        low[i] += dic_DWT97_DELTA * (dic_dwt97_symmetric(high, high_count, i - 1) + dic_dwt97_symmetric(high, high_count, i));

    for (i = 0; i < low_count; ++i)
        low[i] /= dic_DWT97_K;
    for (i = 0; i < high_count; ++i)
        high[i] *= dic_DWT97_K;

    memcpy(samples, scratch, (size_t)length * sizeof(samples[0]));
    return DIC_STATUS_OK;
}

static dic_status dic_dwt97_inverse_1d(double *samples, int length, double *scratch)
{
    int i;
    int low_count;
    int high_count;
    double *low;
    double *high;

    if (samples == NULL || scratch == NULL || length < 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (length <= 1)
        return DIC_STATUS_OK;

    low_count = dic_dwt53_low_size(length);
    high_count = dic_dwt53_high_size(length);
    low = scratch;
    high = scratch + low_count;
    memcpy(scratch, samples, (size_t)length * sizeof(samples[0]));

    for (i = 0; i < low_count; ++i)
        low[i] *= dic_DWT97_K;
    for (i = 0; i < high_count; ++i)
        high[i] /= dic_DWT97_K;

    for (i = 0; i < low_count; ++i)
        low[i] -= dic_DWT97_DELTA * (dic_dwt97_symmetric(high, high_count, i - 1) + dic_dwt97_symmetric(high, high_count, i));
    for (i = 0; i < high_count; ++i)
        high[i] -= dic_DWT97_GAMMA * (dic_dwt97_symmetric(low, low_count, i) + dic_dwt97_symmetric(low, low_count, i + 1));
    for (i = 0; i < low_count; ++i)
        low[i] -= dic_DWT97_BETA * (dic_dwt97_symmetric(high, high_count, i - 1) + dic_dwt97_symmetric(high, high_count, i));
    for (i = 0; i < high_count; ++i)
        high[i] -= dic_DWT97_ALPHA * (dic_dwt97_symmetric(low, low_count, i) + dic_dwt97_symmetric(low, low_count, i + 1));

    for (i = 0; i < low_count; ++i)
        samples[i * 2] = low[i];
    for (i = 0; i < high_count; ++i)
        samples[i * 2 + 1] = high[i];

    return DIC_STATUS_OK;
}

static dic_status dic_dwt97_transform_rows(double *plane, int width, int height, int inverse, double *scratch)
{
    int y;

    for (y = 0; y < height; ++y)
    {
        dic_status status = inverse
            ? dic_dwt97_inverse_1d(plane + (size_t)y * (size_t)width, width, scratch)
            : dic_dwt97_forward_1d(plane + (size_t)y * (size_t)width, width, scratch);
        if (status != DIC_STATUS_OK)
            return status;
    }
    return DIC_STATUS_OK;
}

static dic_status dic_dwt97_transform_columns(double *plane, int width, int height, int inverse, double *scratch)
{
    int x;
    double *column = scratch;
    double *work = scratch + height;

    for (x = 0; x < width; ++x)
    {
        int y;
        dic_status status;

        for (y = 0; y < height; ++y)
            column[y] = plane[(size_t)y * (size_t)width + (size_t)x];
        status = inverse
            ? dic_dwt97_inverse_1d(column, height, work)
            : dic_dwt97_forward_1d(column, height, work);
        if (status != DIC_STATUS_OK)
            return status;
        for (y = 0; y < height; ++y)
            plane[(size_t)y * (size_t)width + (size_t)x] = column[y];
    }
    return DIC_STATUS_OK;
}

static dic_status dic_dwt97_transform_plane(double *plane, int width, int height, int levels, int inverse)
{
    int level;
    int max_dimension;
    double *scratch;
    dic_status status = DIC_STATUS_OK;

    if (plane == NULL || width <= 0 || height <= 0 || levels < 0 || levels > dic_DWT97_MAX_LEVELS)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (dic_dwt53_validate_levels(width, height, levels) != DIC_STATUS_OK)
        return dic_dwt53_validate_levels(width, height, levels);

    max_dimension = width > height ? width : height;
    scratch = (double *)malloc((size_t)max_dimension * 2u * sizeof(scratch[0]));
    if (scratch == NULL)
        return DIC_STATUS_MEMORY_ERROR;

    if (!inverse)
    {
        int current_width = width;
        int current_height = height;

        for (level = 0; level < levels && status == DIC_STATUS_OK; ++level)
        {
            status = dic_dwt97_transform_rows(plane, current_width, current_height, 0, scratch);
            if (status == DIC_STATUS_OK)
                status = dic_dwt97_transform_columns(plane, current_width, current_height, 0, scratch);
            current_width = dic_dwt53_low_size(current_width);
            current_height = dic_dwt53_low_size(current_height);
        }
    }
    else
    {
        int widths[dic_DWT97_MAX_LEVELS + 1];
        int heights[dic_DWT97_MAX_LEVELS + 1];

        widths[0] = width;
        heights[0] = height;
        for (level = 1; level <= levels; ++level)
        {
            widths[level] = dic_dwt53_low_size(widths[level - 1]);
            heights[level] = dic_dwt53_low_size(heights[level - 1]);
        }
        for (level = levels; level > 0 && status == DIC_STATUS_OK; --level)
        {
            status = dic_dwt97_transform_columns(plane, widths[level - 1], heights[level - 1], 1, scratch);
            if (status == DIC_STATUS_OK)
                status = dic_dwt97_transform_rows(plane, widths[level - 1], heights[level - 1], 1, scratch);
        }
    }

    free(scratch);
    return status;
}

dic_status dic_dwt97_forward_plane(double *plane, int width, int height, int levels)
{
    return dic_dwt97_transform_plane(plane, width, height, levels, 0);
}

dic_status dic_dwt97_inverse_plane(double *plane, int width, int height, int levels)
{
    return dic_dwt97_transform_plane(plane, width, height, levels, 1);
}
```

- [ ] **Step 5: Run the wavelet test**

Run:

```text
cmake --build build --target lib_wavelet_test_dwt97
ctest --test-dir build -R "^lib_wavelet_test_dwt97$" --output-on-failure
```

Expected: build succeeds and the test passes.

- [ ] **Step 6: Commit**

Run:

```text
git add code/lib/wavelet/dic_dwt97.h code/lib/wavelet/dic_dwt97.c code/tests/lib_wavelet_test_dwt97.c
git commit -m "feat: add 9-7 wavelet transform"
```

---

### Task 2: Add ICT Module

**Files:**
- Create: `code/lib/j2k/j2k_ict.h`
- Create: `code/lib/j2k/j2k_ict.c`
- Create or modify test: `code/tests/lib_j2k_test_ict.c`

- [ ] **Step 1: Write the failing ICT test**

Create `code/tests/lib_j2k_test_ict.c`:

```c
#include <math.h>
#include <stddef.h>

#include "j2k/j2k_ict.h"
#include "test_helpers.h"

static void dic_expect_close(double actual, double expected, double tolerance)
{
    DIC_EXPECT(fabs(actual - expected) <= tolerance);
}

int main(void)
{
    double pixels[] = {
        12.0, -4.0, 31.0,
        80.0, 17.0, -22.0,
        -50.0, 60.0, 10.0
    };
    double original[sizeof(pixels) / sizeof(pixels[0])];
    size_t i;

    for (i = 0u; i < sizeof(pixels) / sizeof(pixels[0]); ++i)
        original[i] = pixels[i];

    DIC_EXPECT(j2k_ict_forward(pixels, 3u) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_ict_inverse(pixels, 3u) == DIC_STATUS_OK);
    for (i = 0u; i < sizeof(pixels) / sizeof(pixels[0]); ++i)
        dic_expect_close(pixels[i], original[i], 1.0e-6);

    DIC_EXPECT(j2k_ict_forward(NULL, 3u) == DIC_STATUS_INVALID_ARGUMENT);
    return 0;
}
```

- [ ] **Step 2: Run the failing test build**

Run:

```text
cmake --build build --target lib_j2k_test_ict
```

Expected: build fails because `j2k/j2k_ict.h` does not exist.

- [ ] **Step 3: Add the ICT header**

Create `code/lib/j2k/j2k_ict.h`:

```c
#pragma once

#include <stddef.h>

#include "errors/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Applies JPEG 2000 irreversible RGB to YCbCr transform to interleaved double RGB triples. */
dic_status j2k_ict_forward(double *samples, size_t pixel_count);

/** Applies JPEG 2000 irreversible YCbCr to RGB transform to interleaved double triples. */
dic_status j2k_ict_inverse(double *samples, size_t pixel_count);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Add the ICT implementation**

Create `code/lib/j2k/j2k_ict.c`:

```c
#include "j2k/j2k_ict.h"

dic_status j2k_ict_forward(double *samples, size_t pixel_count)
{
    size_t pixel;

    if (samples == NULL && pixel_count > 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (pixel = 0u; pixel < pixel_count; ++pixel)
    {
        double r = samples[pixel * 3u + 0u];
        double g = samples[pixel * 3u + 1u];
        double b = samples[pixel * 3u + 2u];

        samples[pixel * 3u + 0u] = 0.299 * r + 0.587 * g + 0.114 * b;
        samples[pixel * 3u + 1u] = -0.16875 * r - 0.33126 * g + 0.5 * b;
        samples[pixel * 3u + 2u] = 0.5 * r - 0.41869 * g - 0.08131 * b;
    }
    return DIC_STATUS_OK;
}

dic_status j2k_ict_inverse(double *samples, size_t pixel_count)
{
    size_t pixel;

    if (samples == NULL && pixel_count > 0u)
        return DIC_STATUS_INVALID_ARGUMENT;

    for (pixel = 0u; pixel < pixel_count; ++pixel)
    {
        double y = samples[pixel * 3u + 0u];
        double cb = samples[pixel * 3u + 1u];
        double cr = samples[pixel * 3u + 2u];

        samples[pixel * 3u + 0u] = y + 1.402 * cr;
        samples[pixel * 3u + 1u] = y - 0.34413 * cb - 0.71414 * cr;
        samples[pixel * 3u + 2u] = y + 1.772 * cb;
    }
    return DIC_STATUS_OK;
}
```

- [ ] **Step 5: Run the ICT test**

Run:

```text
cmake --build build --target lib_j2k_test_ict
ctest --test-dir build -R "^lib_j2k_test_ict$" --output-on-failure
```

Expected: build succeeds and the test passes.

- [ ] **Step 6: Commit**

Run:

```text
git add code/lib/j2k/j2k_ict.h code/lib/j2k/j2k_ict.c code/tests/lib_j2k_test_ict.c
git commit -m "feat: add JPEG 2000 ICT"
```

---

### Task 3: Add Irreversible Quantization Helpers

**Files:**
- Create: `code/lib/j2k/j2k_quant.h`
- Create: `code/lib/j2k/j2k_quant.c`
- Create: `code/tests/lib_j2k_test_quant.c`

- [ ] **Step 1: Write the failing quantization test**

Create `code/tests/lib_j2k_test_quant.c`:

```c
#include <math.h>
#include <stdint.h>

#include "j2k/j2k_quant.h"
#include "test_helpers.h"

static void dic_expect_close(double actual, double expected, double tolerance)
{
    DIC_EXPECT(fabs(actual - expected) <= tolerance);
}

int main(void)
{
    uint16_t packed;
    uint8_t exponent;
    uint16_t mantissa;
    double step90;
    double step50;
    double step10;
    int32_t quantized;
    double restored;

    DIC_EXPECT(j2k_quant_quality_base_step(90, &step90) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_quant_quality_base_step(50, &step50) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_quant_quality_base_step(10, &step10) == DIC_STATUS_OK);
    DIC_EXPECT(step90 < step50);
    DIC_EXPECT(step50 < step10);
    DIC_EXPECT(j2k_quant_quality_base_step(0, &step50) == DIC_STATUS_INVALID_ARGUMENT);

    DIC_EXPECT(j2k_quant_pack_step(0.25, &packed) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_quant_unpack_step(packed, &exponent, &mantissa, &restored) == DIC_STATUS_OK);
    DIC_EXPECT(exponent > 0u);
    DIC_EXPECT(mantissa <= 2047u);
    dic_expect_close(restored, 0.25, 0.01);

    DIC_EXPECT(j2k_quantize_value(13.2, 0.5, &quantized) == DIC_STATUS_OK);
    DIC_EXPECT(quantized == 26);
    DIC_EXPECT(j2k_dequantize_value(quantized, 0.5, &restored) == DIC_STATUS_OK);
    dic_expect_close(restored, 13.0, 1.0e-9);
    return 0;
}
```

- [ ] **Step 2: Run the failing test build**

Run:

```text
cmake --build build --target lib_j2k_test_quant
```

Expected: build fails because `j2k/j2k_quant.h` does not exist.

- [ ] **Step 3: Add the quantization header**

Create `code/lib/j2k/j2k_quant.h`:

```c
#pragma once

#include <stdint.h>

#include "errors/errors.h"
#include "j2k/j2k_codestream.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    j2k_QUANT_MAX_SUBBANDS = 1 + 3 * j2k_MAX_DECOMPOSITION_LEVELS
};

typedef struct j2k_quant_step
{
    double step;
    uint16_t packed;
} j2k_quant_step;

/** Maps user quality 1..100 to the base irreversible quantization step. */
dic_status j2k_quant_quality_base_step(int quality, double *step);

/** Packs a positive quantization step into JPEG 2000 irreversible SPqcd/SPqcc bits. */
dic_status j2k_quant_pack_step(double step, uint16_t *packed);

/** Unpacks JPEG 2000 irreversible SPqcd/SPqcc bits into exponent, mantissa, and step. */
dic_status j2k_quant_unpack_step(uint16_t packed, uint8_t *exponent, uint16_t *mantissa, double *step);

/** Builds LL/HL/LH/HH quantization steps in packet subband order for a decomposition depth. */
dic_status j2k_quant_build_steps(int quality, int levels, j2k_quant_step *steps, size_t *step_count);

/** Quantizes one transformed coefficient using round-to-nearest scalar quantization. */
dic_status j2k_quantize_value(double value, double step, int32_t *quantized);

/** Dequantizes one scalar coefficient. */
dic_status j2k_dequantize_value(int32_t quantized, double step, double *value);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Add the quantization implementation**

Create `code/lib/j2k/j2k_quant.c`:

```c
#include "j2k/j2k_quant.h"

#include <math.h>
#include <stddef.h>

static double j2k_quant_subband_gain(size_t index)
{
    if (index == 0u)
        return 1.0;
    return (index % 3u) == 0u ? 2.0 : 1.5;
}

dic_status j2k_quant_quality_base_step(int quality, double *step)
{
    if (step == NULL || quality < 1 || quality > 100)
        return DIC_STATUS_INVALID_ARGUMENT;
    *step = pow(2.0, ((double)50 - (double)quality) / 16.0);
    return DIC_STATUS_OK;
}

dic_status j2k_quant_pack_step(double step, uint16_t *packed)
{
    int exponent;
    double normalized;
    int mantissa;

    if (packed == NULL || !(step > 0.0))
        return DIC_STATUS_INVALID_ARGUMENT;
    exponent = (int)ceil(-log(step) / log(2.0));
    if (exponent < 0)
        exponent = 0;
    if (exponent > 31)
        return DIC_STATUS_INVALID_ARGUMENT;
    normalized = step * pow(2.0, (double)exponent);
    mantissa = (int)floor((normalized - 1.0) * 2048.0 + 0.5);
    if (mantissa < 0)
        mantissa = 0;
    if (mantissa > 2047)
        mantissa = 2047;
    *packed = (uint16_t)(((uint16_t)exponent << 11u) | (uint16_t)mantissa);
    return DIC_STATUS_OK;
}

dic_status j2k_quant_unpack_step(uint16_t packed, uint8_t *exponent, uint16_t *mantissa, double *step)
{
    uint8_t e = (uint8_t)(packed >> 11u);
    uint16_t m = (uint16_t)(packed & 0x07ffu);

    if (step == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (exponent != NULL)
        *exponent = e;
    if (mantissa != NULL)
        *mantissa = m;
    *step = (1.0 + (double)m / 2048.0) / pow(2.0, (double)e);
    return DIC_STATUS_OK;
}

dic_status j2k_quant_build_steps(int quality, int levels, j2k_quant_step *steps, size_t *step_count)
{
    double base;
    size_t count;
    size_t index;
    dic_status status;

    if (steps == NULL || step_count == NULL || levels < 0 || levels > j2k_MAX_DECOMPOSITION_LEVELS)
        return DIC_STATUS_INVALID_ARGUMENT;
    status = j2k_quant_quality_base_step(quality, &base);
    if (status != DIC_STATUS_OK)
        return status;
    count = 1u + (size_t)levels * 3u;
    for (index = 0u; index < count; ++index)
    {
        steps[index].step = base * j2k_quant_subband_gain(index);
        status = j2k_quant_pack_step(steps[index].step, &steps[index].packed);
        if (status != DIC_STATUS_OK)
            return status;
    }
    *step_count = count;
    return DIC_STATUS_OK;
}

dic_status j2k_quantize_value(double value, double step, int32_t *quantized)
{
    double scaled;

    if (quantized == NULL || !(step > 0.0))
        return DIC_STATUS_INVALID_ARGUMENT;
    scaled = value / step;
    if (scaled > (double)INT32_MAX || scaled < (double)INT32_MIN)
        return DIC_STATUS_INVALID_ARGUMENT;
    *quantized = (int32_t)(scaled >= 0.0 ? floor(scaled + 0.5) : ceil(scaled - 0.5));
    return DIC_STATUS_OK;
}

dic_status j2k_dequantize_value(int32_t quantized, double step, double *value)
{
    if (value == NULL || !(step > 0.0))
        return DIC_STATUS_INVALID_ARGUMENT;
    *value = (double)quantized * step;
    return DIC_STATUS_OK;
}
```

- [ ] **Step 5: Run the quantization test**

Run:

```text
cmake --build build --target lib_j2k_test_quant
ctest --test-dir build -R "^lib_j2k_test_quant$" --output-on-failure
```

Expected: build succeeds and the test passes.

- [ ] **Step 6: Commit**

Run:

```text
git add code/lib/j2k/j2k_quant.h code/lib/j2k/j2k_quant.c code/tests/lib_j2k_test_quant.c
git commit -m "feat: add JPEG 2000 quantization helpers"
```

---

### Task 4: Extend Codestream Metadata and Marker Syntax

**Files:**
- Modify: `code/lib/j2k/j2k_codestream.h`
- Modify: `code/lib/j2k/j2k_codestream.c`
- Modify: `code/lib/j2k/j2k_parse.c`
- Modify: `code/tests/lib_j2k_test_codestream.c`
- Modify: `code/tests/lib_j2k_test_parse.c`

- [ ] **Step 1: Write failing marker/parser tests**

In `code/tests/lib_j2k_test_codestream.c`, add a second parameter setup near the existing reversible `params` setup:

```c
j2k_basic_params lossy_params = {0};
lossy_params.width = 64u;
lossy_params.height = 48u;
lossy_params.components = 1u;
lossy_params.decomposition_levels = 2u;
lossy_params.reversible = 0u;
lossy_params.multiple_component_transform = 0u;
lossy_params.quant_step_count = 7u;
for (marker = 0u; marker < lossy_params.quant_step_count; ++marker)
{
    lossy_params.quant_steps[marker].packed = (uint16_t)((8u << 11u) | marker);
    lossy_params.quant_steps[marker].step = 1.0;
}
DIC_EXPECT(j2k_write_minimal_codestream("dic_lossy_marker_test.j2k", &lossy_params) == DIC_STATUS_OK);
```

Then scan the file and assert:

```c
/* Inside marker scan for dic_lossy_marker_test.j2k */
DIC_EXPECT(transform == 0);
DIC_EXPECT(sqcd == 0x40);
DIC_EXPECT(length == 5u + 6u * lossy_params.decomposition_levels);
```

In `code/tests/lib_j2k_test_parse.c`, after the existing parse assertions add:

```c
params.reversible = 0u;
params.quant_step_count = 1u + 3u * params.decomposition_levels;
for (resolution = 0u; resolution < params.quant_step_count; ++resolution)
{
    params.quant_steps[resolution].packed = (uint16_t)((8u << 11u) | resolution);
    params.quant_steps[resolution].step = 1.0;
}
DIC_EXPECT(j2k_write_empty_packet_codestream(j2k_path, &params) == DIC_STATUS_OK);
DIC_EXPECT(j2k_read_codestream_info(j2k_path, &info) == DIC_STATUS_OK);
DIC_EXPECT(info.params.reversible == 0u);
DIC_EXPECT(info.params.quant_step_count == params.quant_step_count);
DIC_EXPECT(info.params.quant_steps[0].packed == params.quant_steps[0].packed);
```

- [ ] **Step 2: Run failing tests**

Run:

```text
cmake --build build --target lib_j2k_test_codestream lib_j2k_test_parse
ctest --test-dir build -R "^lib_j2k_test_(codestream|parse)$" --output-on-failure
```

Expected: build fails because `j2k_basic_params` has no `quant_step_count` or `quant_steps`.

- [ ] **Step 3: Extend `j2k_basic_params`**

Modify `code/lib/j2k/j2k_codestream.h`:

```c
#include "j2k/j2k_quant.h"
```

This include would cycle because `j2k_quant.h` includes `j2k_codestream.h`; instead move `j2k_QUANT_MAX_SUBBANDS` and `j2k_quant_step` into `j2k_codestream.h`, then include `j2k_codestream.h` from `j2k_quant.h`. Add:

```c
enum
{
    j2k_MAX_QUANT_STEPS = 1 + 3 * j2k_MAX_DECOMPOSITION_LEVELS
};

typedef struct j2k_quant_step
{
    double step;
    uint16_t packed;
} j2k_quant_step;
```

Add fields to `j2k_basic_params`:

```c
uint8_t quant_step_count; /**< Number of valid QCD/QCC subband quantization steps. */
j2k_quant_step quant_steps[j2k_MAX_QUANT_STEPS]; /**< Irreversible SPqcd/SPqcc steps in packet subband order. */
```

Then remove the duplicate enum/typedef from `j2k_quant.h`.

- [ ] **Step 4: Write irreversible QCD/QCC**

In `code/lib/j2k/j2k_codestream.c`, split reversible and irreversible QCD writing:

```c
static int j2k_write_qcd_irreversible(FILE *file, const j2k_basic_params *params)
{
    uint8_t index;
    uint16_t length = (uint16_t)(3u + 2u * params->quant_step_count);

    if (params->quant_step_count != 1u + 3u * params->decomposition_levels)
        return 0;
    if (!j2k_write_marker(file, j2k_MARKER_QCD)
        || !j2k_write_u16_be(file, length)
        || !j2k_write_u8(file, 0x40u))
    {
        return 0;
    }
    for (index = 0u; index < params->quant_step_count; ++index)
    {
        if (!j2k_write_u16_be(file, params->quant_steps[index].packed))
            return 0;
    }
    return 1;
}
```

Change `j2k_write_qcd` to:

```c
if (!params->reversible)
    return j2k_write_qcd_irreversible(file, params);
```

For QCC, keep the existing reversible RGB extra precision path only when `params->reversible`. Do not write QCC for lossy RGB in this task; shared QCD is enough for the first image tests.

- [ ] **Step 5: Parse irreversible QCD**

In `code/lib/j2k/j2k_parse.c`, update QCD parsing so `Sqcd == 0x40` with 16-bit entries is accepted when `info->params.reversible == 0`. Use the marker length to distinguish reversible 8-bit SPqcd entries from irreversible 16-bit SPqcd entries:

```c
static dic_status j2k_parse_qcd(FILE *file, uint16_t length, j2k_codestream_info *info)
{
    uint8_t sqcd;
    uint16_t remaining;

    if (length < 3u || info == NULL)
        return DIC_J2K_FORMAT_ERROR;
    if (!j2k_read_u8(file, &sqcd))
        return DIC_STATUS_FILE_READ_ERROR;
    remaining = (uint16_t)(length - 3u);
    if (info->params.reversible)
    {
        return j2k_skip_bytes(file, remaining);
    }
    if ((remaining % 2u) != 0u)
        return DIC_J2K_FORMAT_ERROR;
    info->params.quant_step_count = (uint8_t)(remaining / 2u);
    for (uint8_t index = 0u; index < info->params.quant_step_count; ++index)
    {
        uint16_t packed;
        if (!j2k_read_u16_be(file, &packed))
            return DIC_STATUS_FILE_READ_ERROR;
        info->params.quant_steps[index].packed = packed;
        if (j2k_quant_unpack_step(packed, NULL, NULL, &info->params.quant_steps[index].step) != DIC_STATUS_OK)
            return DIC_J2K_FORMAT_ERROR;
    }
    return DIC_STATUS_OK;
}
```

Use `j2k_read_u8`, `j2k_read_u16_be`, and `fseek(file, (long)remaining, SEEK_CUR)` because those are the existing local parse helpers in `j2k_parse.c`.

- [ ] **Step 6: Run marker/parser tests**

Run:

```text
cmake --build build --target lib_j2k_test_codestream lib_j2k_test_parse
ctest --test-dir build -R "^lib_j2k_test_(codestream|parse)$" --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 7: Commit**

Run:

```text
git add code/lib/j2k/j2k_codestream.h code/lib/j2k/j2k_codestream.c code/lib/j2k/j2k_parse.c code/lib/j2k/j2k_quant.h code/tests/lib_j2k_test_codestream.c code/tests/lib_j2k_test_parse.c
git commit -m "feat: write irreversible J2K quantization markers"
```

---

### Task 5: Extend Public Encode API and Preserve Lossless Behavior

**Files:**
- Modify: `code/lib/j2k/j2k_image.h`
- Modify: `code/lib/j2k/j2k_image.c`
- Modify: `code/finalproj/finalproj_codec.h`
- Modify: `code/finalproj/finalproj_codec.c`
- Modify: `code/tests/lib_j2k_test_image.c`

- [ ] **Step 1: Update tests to expect the new API**

In `code/tests/lib_j2k_test_image.c`, change existing lossless calls:

```c
DIC_EXPECT(j2k_write_image_codestream(j2k_path, &gray, 5, -1) == DIC_STATUS_OK);
DIC_EXPECT(j2k_write_image_jp2(jp2_path, &rgb, 5, -1) == DIC_STATUS_OK);
```

Add invalid quality checks after image allocation:

```c
DIC_EXPECT(j2k_write_image_codestream("invalid_quality.j2k", &gray, 5, 0) == DIC_STATUS_INVALID_ARGUMENT);
DIC_EXPECT(j2k_write_image_jp2("invalid_quality.jp2", &rgb, 5, 101) == DIC_STATUS_INVALID_ARGUMENT);
remove("invalid_quality.j2k");
remove("invalid_quality.jp2");
```

- [ ] **Step 2: Run failing build**

Run:

```text
cmake --build build --target lib_j2k_test_image
```

Expected: build fails because `j2k_write_image_codestream` and `j2k_write_image_jp2` still take three arguments.

- [ ] **Step 3: Change public declarations**

Modify `code/lib/j2k/j2k_image.h`:

```c
dic_status j2k_write_image_codestream(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int quality
);

dic_status j2k_write_image_jp2(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int quality
);
```

Update function docs to state `quality == -1` is lossless and `1..100` is lossy.

- [ ] **Step 4: Change implementation signatures and validation**

Modify `code/lib/j2k/j2k_image.c`:

```c
static dic_status j2k_image_validate_quality(int quality)
{
    if (quality == -1 || (quality >= 1 && quality <= 100))
        return DIC_STATUS_OK;
    return DIC_STATUS_INVALID_ARGUMENT;
}
```

Update `j2k_write_image_codestream` and `j2k_write_image_jp2` signatures. At the top of each:

```c
status = j2k_image_validate_quality(quality);
if (status != DIC_STATUS_OK)
{
    j2k_image_payload_free(&payload);
    return status;
}
if (quality != -1)
{
    j2k_image_payload_free(&payload);
    return DIC_J2K_FORMAT_ERROR;
}
```

Use `DIC_J2K_FORMAT_ERROR` only as a temporary red test bridge if no better unsupported status exists. Task 7 replaces this with the real lossy path.

- [ ] **Step 5: Update final project wrappers**

Modify `code/finalproj/finalproj_codec.h`:

```c
int imageWriteJ2K(const char *orgImageFileName, const char *outputFileName, int quality);
int imageWriteJP2(const char *orgImageFileName, const char *outputFileName, int quality);
```

Modify `code/finalproj/finalproj_codec.c`:

```c
int imageWriteJ2K(const char *orgImageFileName, const char *outputFileName, int quality)
{
    ...
    status = j2k_write_image_codestream(outputFileName, &image, FINALPROJ_LEVELS, quality);
    ...
}

int imageWriteJP2(const char *orgImageFileName, const char *outputFileName, int quality)
{
    ...
    status = j2k_write_image_jp2(outputFileName, &image, FINALPROJ_LEVELS, quality);
    ...
}
```

- [ ] **Step 6: Update all direct call sites**

In `code/finalproj/main.c`, temporarily pass `-1`:

```c
: imageWriteJ2K(argv[2], argv[3], -1);
: imageWriteJP2(argv[2], argv[3], -1);
```

In tests and any other call sites found by:

```text
rg -n "j2k_write_image_codestream\\(|j2k_write_image_jp2\\(" code
```

add `-1` unless the test intentionally exercises lossy mode.

- [ ] **Step 7: Run lossless regression tests**

Run:

```text
cmake --build build --target lib_j2k_test_image finalproj
ctest --test-dir build -R "^lib_j2k_test_image$" --output-on-failure
```

Expected: lossless image tests still pass; invalid quality returns `DIC_STATUS_INVALID_ARGUMENT`.

- [ ] **Step 8: Commit**

Run:

```text
git add code/lib/j2k/j2k_image.h code/lib/j2k/j2k_image.c code/finalproj/finalproj_codec.h code/finalproj/finalproj_codec.c code/finalproj/main.c code/tests/lib_j2k_test_image.c
git commit -m "feat: add quality parameter to J2K encoders"
```

---

### Task 6: Add CLI Optional Q Parsing

**Files:**
- Modify: `code/finalproj/main.c`
- Modify: `code/finalproj/finalproj_codec.h`
- Modify: `code/finalproj/finalproj_codec.c`

- [ ] **Step 1: Add CLI parsing tests by manual command expectations**

No dedicated CLI test harness exists. Use finalproj build plus manual command checks in this task.

Expected command behavior after implementation:

```text
finalproj j2k-encode input.ppm output.j2k
finalproj j2k-encode input.ppm output.j2k 75
finalproj jp2-encode input.ppm output.jp2
finalproj jp2-encode input.ppm output.jp2 75
```

The first and third pass `quality=-1`; the second and fourth pass `quality=75`.

- [ ] **Step 2: Update usage text**

Modify `finalproj_print_usage` in `code/finalproj/main.c`:

```c
"  finalproj j2k-encode <input.pgm|input.ppm> <output.j2k> [Q]\n"
"  finalproj jp2-encode <input.pgm|input.ppm> <output.jp2> [Q]\n"
```

- [ ] **Step 3: Add a quality parser**

Add near `finalproj_parse_positive_int`:

```c
static int finalproj_parse_j2k_quality(int argc, char **argv, int *quality)
{
    int parsed = 0;

    if (quality == NULL)
        return 0;
    *quality = -1;
    if (argc == 4)
        return 1;
    if (argc != 5)
        return 0;
    if (!finalproj_parse_positive_int(argv[4], &parsed) || parsed < 1 || parsed > 100)
        return 0;
    *quality = parsed;
    return 1;
}
```

- [ ] **Step 4: Use optional Q in write commands**

Modify the `j2k-encode` and `jp2-encode` blocks:

```c
int quality = -1;
if (strcmp(argv[1], "j2k-stub") == 0 || strcmp(argv[1], "j2k-encode") == 0)
{
    if (strcmp(argv[1], "j2k-stub") == 0)
    {
        if (argc != 4)
            return 0;
        ok = imageWriteJ2KStub(argv[2], argv[3]);
    }
    else
    {
        if (!finalproj_parse_j2k_quality(argc, argv, &quality))
            return 0;
        ok = imageWriteJ2K(argv[2], argv[3], quality);
    }
    ...
}
```

Apply the same shape to `jp2-encode`.

- [ ] **Step 5: Build finalproj**

Run:

```text
cmake --build build --target finalproj
```

Expected: build succeeds.

- [ ] **Step 6: Commit**

Run:

```text
git add code/finalproj/main.c code/finalproj/finalproj_codec.h code/finalproj/finalproj_codec.c
git commit -m "feat: accept JPEG 2000 quality argument"
```

---

### Task 7: Implement Lossy Encode Path

**Files:**
- Modify: `code/lib/j2k/j2k_image.c`
- Modify: `code/tests/lib_j2k_test_image.c`

- [ ] **Step 1: Add failing lossy encode/decode tests**

In `code/tests/lib_j2k_test_image.c`, include metrics:

```c
#include "codec/dic_metrics.h"
```

Add after the gray lossless test:

```c
const char *lossy90_path = "dic_image_lossy_q90.j2k";
const char *lossy50_path = "dic_image_lossy_q50.j2k";
dic_image_u8 decoded90;
dic_image_u8 decoded50;
double psnr90;
double psnr50;

dic_image_u8_init(&decoded90);
dic_image_u8_init(&decoded50);
DIC_EXPECT(j2k_write_image_codestream(lossy90_path, &gray, 5, 90) == DIC_STATUS_OK);
DIC_EXPECT(j2k_write_image_codestream(lossy50_path, &gray, 5, 50) == DIC_STATUS_OK);
DIC_EXPECT(j2k_read_codestream_info(lossy90_path, &info) == DIC_STATUS_OK);
DIC_EXPECT(info.params.reversible == 0u);
DIC_EXPECT(info.params.quant_step_count == 1u + 3u * info.params.decomposition_levels);
DIC_EXPECT(j2k_read_image_codestream(lossy90_path, &decoded90) == DIC_STATUS_OK);
DIC_EXPECT(j2k_read_image_codestream(lossy50_path, &decoded50) == DIC_STATUS_OK);
DIC_EXPECT(decoded90.width == gray.width);
DIC_EXPECT(decoded90.height == gray.height);
DIC_EXPECT(decoded90.channels == gray.channels);
psnr90 = dic_metric_psnr_u8(gray.data, decoded90.data, dic_image_u8_sample_count(gray.width, gray.height, gray.channels));
psnr50 = dic_metric_psnr_u8(gray.data, decoded50.data, dic_image_u8_sample_count(gray.width, gray.height, gray.channels));
DIC_EXPECT(psnr90 > psnr50);
DIC_EXPECT(psnr90 > 20.0);
dic_image_u8_free(&decoded90);
dic_image_u8_free(&decoded50);
remove(lossy90_path);
remove(lossy50_path);
```

Add a similar RGB JP2 lossy smoke near the RGB section:

```c
DIC_EXPECT(j2k_write_image_jp2(jp2_path, &rgb, 5, 75) == DIC_STATUS_OK);
DIC_EXPECT(jp2_read_codestream_info(jp2_path, &info) == DIC_STATUS_OK);
DIC_EXPECT(info.params.reversible == 0u);
DIC_EXPECT(j2k_read_image_jp2(jp2_path, &decoded) == DIC_STATUS_OK);
DIC_EXPECT(decoded.width == rgb.width);
DIC_EXPECT(decoded.height == rgb.height);
DIC_EXPECT(decoded.channels == rgb.channels);
dic_image_u8_free(&decoded);
remove(jp2_path);
```

- [ ] **Step 2: Run the failing image test**

Run:

```text
cmake --build build --target lib_j2k_test_image
ctest --test-dir build -R "^lib_j2k_test_image$" --output-on-failure
```

Expected: lossy encode returns unsupported status or decode fails.

- [ ] **Step 3: Add double-plane creation helpers**

In `code/lib/j2k/j2k_image.c`, include:

```c
#include "j2k/j2k_ict.h"
#include "j2k/j2k_quant.h"
#include "wavelet/dic_dwt97.h"
```

Add helper:

```c
static dic_status j2k_image_make_double_planes(
    const dic_image_u8 *image,
    double **planes_out
)
{
    size_t pixel_count;
    double *planes;
    size_t pixel;
    int component;

    if (image == NULL || planes_out == NULL || image->width <= 0 || image->height <= 0
        || (image->channels != 1 && image->channels != 3))
    {
        return DIC_STATUS_INVALID_ARGUMENT;
    }
    pixel_count = (size_t)image->width * (size_t)image->height;
    planes = (double *)malloc(pixel_count * (size_t)image->channels * sizeof(planes[0]));
    if (planes == NULL)
        return DIC_STATUS_MEMORY_ERROR;
    if (image->channels == 1)
    {
        for (pixel = 0u; pixel < pixel_count; ++pixel)
            planes[pixel] = (double)image->data[pixel] - 128.0;
    }
    else
    {
        double *interleaved = (double *)malloc(pixel_count * 3u * sizeof(interleaved[0]));
        dic_status status;

        if (interleaved == NULL)
        {
            free(planes);
            return DIC_STATUS_MEMORY_ERROR;
        }
        for (pixel = 0u; pixel < pixel_count; ++pixel)
        {
            for (component = 0; component < 3; ++component)
                interleaved[pixel * 3u + (size_t)component] =
                    (double)image->data[pixel * 3u + (size_t)component] - 128.0;
        }
        status = j2k_ict_forward(interleaved, pixel_count);
        if (status != DIC_STATUS_OK)
        {
            free(interleaved);
            free(planes);
            return status;
        }
        for (pixel = 0u; pixel < pixel_count; ++pixel)
        {
            for (component = 0; component < 3; ++component)
                planes[(size_t)component * pixel_count + pixel] =
                    interleaved[pixel * 3u + (size_t)component];
        }
        free(interleaved);
    }
    *planes_out = planes;
    return DIC_STATUS_OK;
}
```

- [ ] **Step 4: Add lossy subband quantization to EBCOT stream building**

Add an image helper that mirrors `j2k_image_append_subband_streams`, but reads `double *plane`, quantizes each coefficient with a passed `j2k_quant_step`, then calls `j2k_ebcot_encode_codeblock_rect` with the integer block.

Use this core inside the helper:

```c
status = j2k_quantize_value(
    plane[(size_t)(rect->y + by + y) * (size_t)plane_width + (size_t)(rect->x + bx + x)],
    quant_step->step,
    block + (size_t)y * (size_t)block_width + (size_t)x
);
```

Set `stream->zero_bitplanes` using the quantized coefficient magnitude exactly like the reversible path does.

- [ ] **Step 5: Add lossy payload builder**

Add:

```c
static dic_status j2k_image_encode_payload_lossy(
    const dic_image_u8 *image,
    int requested_levels,
    int quality,
    j2k_basic_params *params,
    j2k_image_payload *payload
)
```

Implementation outline:

```c
double *planes = NULL;
j2k_quant_step steps[j2k_MAX_QUANT_STEPS];
size_t step_count = 0u;
int levels = j2k_image_effective_levels(image->width, image->height, requested_levels);
size_t plane_samples = (size_t)image->width * (size_t)image->height;

status = j2k_image_make_double_planes(image, &planes);
if (status == DIC_STATUS_OK)
    status = j2k_quant_build_steps(quality, levels, steps, &step_count);
for (component = 0; status == DIC_STATUS_OK && component < image->channels; ++component)
    status = dic_dwt97_forward_plane(planes + (size_t)component * plane_samples, image->width, image->height, levels);
if (status == DIC_STATUS_OK)
    status = j2k_image_build_payload_lossy(planes, image->width, image->height, image->channels, levels, steps, step_count, 1u, payload);
if (status == DIC_STATUS_OK)
{
    params->width = (uint32_t)image->width;
    params->height = (uint32_t)image->height;
    params->components = (uint16_t)image->channels;
    params->decomposition_levels = (uint8_t)levels;
    params->reversible = 0u;
    params->multiple_component_transform = image->channels == 3 ? 1u : 0u;
    params->layers = 1u;
    params->quant_step_count = (uint8_t)step_count;
    memcpy(params->quant_steps, steps, step_count * sizeof(steps[0]));
    params->use_sop = 1u;
    params->use_eph = 1u;
    j2k_image_set_max_precincts(params);
}
free(planes);
```

Make `j2k_image_build_payload_lossy` mirror LRCP ordering from `j2k_image_build_payload`; in this first phase use one layer.

- [ ] **Step 6: Route quality mode**

In `j2k_write_image_codestream`:

```c
if (status == DIC_STATUS_OK)
{
    status = quality == -1
        ? j2k_image_encode_payload(image, requested_levels, 1u, NULL, 0u, &params, &payload)
        : j2k_image_encode_payload_lossy(image, requested_levels, quality, &params, &payload);
}
```

Do the same in `j2k_write_image_jp2`.

- [ ] **Step 7: Run image tests**

Run:

```text
cmake --build build --target lib_j2k_test_image
ctest --test-dir build -R "^lib_j2k_test_image$" --output-on-failure
```

Expected: encode succeeds, decode may still fail until Task 8. If decode fails because irreversible decode is not implemented, commit is not ready; continue directly to Task 8 before committing.

---

### Task 8: Implement Lossy Decode Path

**Files:**
- Modify: `code/lib/j2k/j2k_decode.c`
- Modify: `code/tests/lib_j2k_test_image.c`

- [ ] **Step 1: Confirm the failing decode test**

Run:

```text
ctest --test-dir build -R "^lib_j2k_test_image$" --output-on-failure
```

Expected: failure points to irreversible decode path, QCD parsing, or inverse transform.

- [ ] **Step 2: Add double dequantized subband reconstruction**

In `code/lib/j2k/j2k_decode.c`, include:

```c
#include "j2k/j2k_ict.h"
#include "j2k/j2k_quant.h"
#include "wavelet/dic_dwt97.h"
```

Add a helper parallel to `j2k_decode_subbands_to_planes`, but writing `double *planes`:

```c
static dic_status j2k_decode_subbands_to_double_planes(
    j2k_decode_subband *subbands,
    size_t subband_count,
    const j2k_basic_params *params,
    double *planes,
    int width,
    int height,
    int components
)
```

Inside each decoded code-block loop, after `j2k_ebcot_decode_codeblock_rect`, dequantize each integer coefficient:

```c
size_t quant_index = subband_index % (1u + (size_t)params->decomposition_levels * 3u);
double value;
status = j2k_dequantize_value(decoded[(size_t)y * block->stream.width + x],
    params->quant_steps[quant_index].step,
    &value);
plane[(size_t)dst_y * (size_t)width + (size_t)dst_x + x] = value;
```

- [ ] **Step 3: Add lossy tile payload decode**

Create:

```c
static dic_status j2k_decode_tile_payload_lossy(
    const uint8_t *payload,
    size_t payload_size,
    const j2k_basic_params *params,
    uint16_t max_layers,
    int tile_width,
    int tile_height,
    dic_image_u8 *tile
)
```

Use the same packet reading loop as `j2k_decode_tile_payload`. After packet reading:

```c
status = j2k_decode_subbands_to_double_planes(...);
for (component = 0; status == DIC_STATUS_OK && component < (int)params->components; ++component)
    status = dic_dwt97_inverse_plane(planes + (size_t)component * plane_samples, tile_width, tile_height, (int)params->decomposition_levels);
```

For RGB with MCT:

```c
double *interleaved = malloc(plane_samples * 3u * sizeof(interleaved[0]));
...
status = j2k_ict_inverse(interleaved, plane_samples);
tile->data[pixel * 3u + c] = j2k_decode_unshift_u8((int32_t)floor(interleaved[pixel * 3u + c] + 0.5));
```

For gray:

```c
tile->data[sample] = j2k_decode_unshift_u8((int32_t)floor(planes[sample] + 0.5));
```

- [ ] **Step 4: Route decode by transform mode**

In `j2k_decode_image_from_codestream`, when decoding each tile:

```c
status = params.reversible
    ? j2k_decode_tile_payload(...)
    : j2k_decode_tile_payload_lossy(...);
```

Before tile loop, validate:

```c
if (!params.reversible && params.quant_step_count != 1u + 3u * params.decomposition_levels)
    status = DIC_J2K_FORMAT_ERROR;
```

- [ ] **Step 5: Run lossy image test**

Run:

```text
cmake --build build --target lib_j2k_test_image
ctest --test-dir build -R "^lib_j2k_test_image$" --output-on-failure
```

Expected: lossless and lossy image tests pass.

- [ ] **Step 6: Commit encode/decode together**

Because Task 7 cannot be complete without decode, commit both tasks now:

```text
git add code/lib/j2k/j2k_image.c code/lib/j2k/j2k_decode.c code/tests/lib_j2k_test_image.c
git commit -m "feat: add JPEG 2000 9-7 quality mode"
```

---

### Task 9: Update CLI Smoke and Report

**Files:**
- Modify: `report.html`

- [ ] **Step 1: Run finalproj smoke manually**

Create a temporary 4x4 binary PPM in the workspace, run the CLI, then remove the smoke files:

```text
powershell -ExecutionPolicy Bypass -Command "$bytes = [byte[]](80,54,10,52,32,52,10,50,53,53,10); for ($i=0; $i -lt 16; $i++) { $bytes += [byte]($i * 11); $bytes += [byte](255 - $i * 7); $bytes += [byte]($i * 3) }; [IO.File]::WriteAllBytes('dic_lossy_smoke.ppm', $bytes)"
cmake --build build --target finalproj
.\build\bin\finalproj.exe jp2-encode dic_lossy_smoke.ppm dic_lossy_smoke.jp2 75
.\build\bin\finalproj.exe jp2-decode dic_lossy_smoke.jp2 dic_lossy_smoke.ppm
.\build\bin\finalproj.exe jp2-info dic_lossy_smoke.jp2
powershell -ExecutionPolicy Bypass -Command "Remove-Item dic_lossy_smoke.ppm,dic_lossy_smoke.jp2 -ErrorAction SilentlyContinue"
```

Expected: encode/decode commands succeed and info prints `reversible 0`.

- [ ] **Step 2: Update `report.html`**

In `report.html`, update the summary and capability boundary sections to say:

```html
<p>
  <code>Q=-1</code> keeps the existing 5/3 reversible path. A quality value in
  <code>1..100</code> selects the new 9/7 irreversible path with ICT for RGB and
  JPEG 2000 irreversible QCD/QCC quantization syntax.
</p>
```

Add ROI note:

```html
<p>
  ROI Maxshift remains limited to the reversible path in this phase. The 9/7 path
  deliberately rejects ROI integration until decode-side ROI scaling is designed.
</p>
```

- [ ] **Step 3: Run full verification**

Run:

```text
cmake --build build --target project_tests
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 4: Commit docs**

Run:

```text
git add report.html
git commit -m "docs: document JPEG 2000 quality mode"
```

---

## Plan Self-Review

- Spec coverage: API, CLI, 9/7 under `lib/wavelet`, ICT, irreversible QCD/QCC, encode/decode, tests, and report update all have tasks.
- Scope guard: ROI and tiled lossy are explicitly excluded from implementation tasks.
- Type consistency: `quality` is `int`; `Q=-1` is library-level lossless; `1..100` is lossy.
- Build consistency: new library and test files rely on the existing recursive CMake source glob.
- Verification: task-level tests plus final `project_tests` and full `ctest` are specified.

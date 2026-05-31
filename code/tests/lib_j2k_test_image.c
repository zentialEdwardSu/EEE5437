#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image_u8/image_u8.h"
#include "j2k/j2k_image.h"
#include "j2k/j2k_parse.h"
#include "j2k/j2k_roi.h"
#include "test_helpers.h"

static int dic_test_read_u16_be(FILE *file, unsigned int *value)
{
    int hi = fgetc(file);
    int lo = fgetc(file);

    if (hi == EOF || lo == EOF)
        return 0;
    *value = ((unsigned int)hi << 8) | (unsigned int)lo;
    return 1;
}

static int dic_test_j2k_payload_starts_with_sop_and_has_eph(
    const char *path,
    size_t payload_bytes
)
{
    FILE *file = NULL;
    unsigned int marker = 0u;
    size_t index;
    int previous = 0;
    int saw_eph = 0;

#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb") != 0)
        return 0;
#else
    file = fopen(path, "rb");
    if (file == NULL)
        return 0;
#endif

    while (dic_test_read_u16_be(file, &marker))
    {
        unsigned int length;

        if (marker == j2k_MARKER_SOD)
            break;
        if (marker == j2k_MARKER_SOC)
            continue;
        if (!dic_test_read_u16_be(file, &length) || fseek(file, (long)length - 2L, SEEK_CUR) != 0)
        {
            fclose(file);
            return 0;
        }
    }

    if (marker != j2k_MARKER_SOD)
    {
        fclose(file);
        return 0;
    }
    if (!dic_test_read_u16_be(file, &marker) || marker != j2k_MARKER_SOP)
    {
        fclose(file);
        return 0;
    }
    if (!dic_test_read_u16_be(file, &marker) || marker != 4u)
    {
        fclose(file);
        return 0;
    }
    if (!dic_test_read_u16_be(file, &marker) || marker != 0u)
    {
        fclose(file);
        return 0;
    }

    for (index = 6u; index < payload_bytes; ++index)
    {
        int byte = fgetc(file);

        if (byte == EOF)
            break;
        if (previous == 0xff && byte == 0x92)
        {
            saw_eph = 1;
            break;
        }
        previous = byte;
    }

    fclose(file);
    return saw_eph;
}

static int dic_test_seek_to_sod_payload(FILE *file)
{
    unsigned int marker = 0u;

    if (!dic_test_read_u16_be(file, &marker) || marker != j2k_MARKER_SOC)
        return 0;
    while (dic_test_read_u16_be(file, &marker))
    {
        unsigned int length;

        if (marker == j2k_MARKER_SOD)
            return 1;
        if (marker == j2k_MARKER_EOC)
            return 0;
        if (!dic_test_read_u16_be(file, &length) || length < 2u)
            return 0;
        if (fseek(file, (long)length - 2L, SEEK_CUR) != 0)
            return 0;
    }
    return 0;
}

static int dic_test_j2k_sod_payload_differs(
    const char *left_path,
    size_t left_payload_bytes,
    const char *right_path,
    size_t right_payload_bytes
)
{
    FILE *left = NULL;
    FILE *right = NULL;
    size_t compared_bytes;
    size_t index;
    int differs = 0;

#if defined(_MSC_VER)
    if (fopen_s(&left, left_path, "rb") != 0)
        return 0;
    if (fopen_s(&right, right_path, "rb") != 0)
    {
        fclose(left);
        return 0;
    }
#else
    left = fopen(left_path, "rb");
    right = fopen(right_path, "rb");
    if (left == NULL || right == NULL)
    {
        if (left != NULL)
            fclose(left);
        if (right != NULL)
            fclose(right);
        return 0;
    }
#endif

    if (!dic_test_seek_to_sod_payload(left) || !dic_test_seek_to_sod_payload(right))
    {
        fclose(left);
        fclose(right);
        return 0;
    }

    compared_bytes = left_payload_bytes < right_payload_bytes ? left_payload_bytes : right_payload_bytes;
    for (index = 0u; index < compared_bytes; ++index)
    {
        int left_byte = fgetc(left);
        int right_byte = fgetc(right);

        if (left_byte == EOF || right_byte == EOF)
            break;
        if (left_byte != right_byte)
        {
            differs = 1;
            break;
        }
    }
    if (!differs && left_payload_bytes != right_payload_bytes)
        differs = 1;

    fclose(left);
    fclose(right);
    return differs;
}

static unsigned int dic_test_max_abs_diff(const dic_image_u8 *left, const dic_image_u8 *right)
{
    size_t count;
    size_t index;
    unsigned int max_diff = 0u;

    if (left->width != right->width || left->height != right->height || left->channels != right->channels)
        return 256u;

    count = dic_image_u8_sample_count(left->width, left->height, left->channels);
    for (index = 0u; index < count; ++index)
    {
        unsigned int left_sample = left->data[index];
        unsigned int right_sample = right->data[index];
        unsigned int diff = left_sample > right_sample
            ? left_sample - right_sample
            : right_sample - left_sample;

        if (diff > max_diff)
            max_diff = diff;
    }
    return max_diff;
}

/* Reference: paper/T-REC-T.800-200208.pdf, Annex G, Annex F, Annex D, and Annex B.10, image samples become packetized EBCOT tile-part payload. */
int main(void)
{
    const char *j2k_path = "dic_image_real_payload_test.j2k";
    const char *lossy_j2k_path = "dic_image_lossy_payload_test.j2k";
    const char *roi_j2k_path = "dic_image_roi_payload_test.j2k";
    const char *tiled_j2k_path = "dic_image_tiled_payload_test.j2k";
    const char *jp2_path = "dic_image_real_payload_test.jp2";
    const char *tiled_jp2_path = "dic_image_tiled_payload_test.jp2";
    dic_image_u8 gray;
    dic_image_u8 rgb;
    dic_image_u8 decoded;
    j2k_codestream_info info;
    j2k_codestream_info roi_info;
    dic_rect_i32 roi_rect = { 2, 2, 3, 3 };
    uint8_t *roi_shift_map = NULL;
    const int lossy_qualities[] = {1, 75, 100};
    int x;
    int y;
    dic_status status;
    unsigned int resolution;

    dic_image_u8_init(&gray);
    dic_image_u8_init(&rgb);
    dic_image_u8_init(&decoded);

    DIC_EXPECT(dic_image_u8_alloc(&gray, 8, 8, 1) == DIC_STATUS_OK);
    for (y = 0; y < gray.height; ++y)
    {
        for (x = 0; x < gray.width; ++x)
            gray.data[(size_t)y * (size_t)gray.width + (size_t)x] = (uint8_t)(x * 17 + y * 11);
    }

    DIC_EXPECT(j2k_write_image_codestream(j2k_path, &gray, 5, -1) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_read_codestream_info(j2k_path, &info) == DIC_STATUS_OK);
    DIC_EXPECT(info.params.width == 8u);
    DIC_EXPECT(info.params.height == 8u);
    DIC_EXPECT(info.params.components == 1u);
    DIC_EXPECT(info.params.decomposition_levels == 3u);
    DIC_EXPECT(info.params.use_sop == 1u);
    DIC_EXPECT(info.params.use_eph == 1u);
    DIC_EXPECT(info.params.use_precincts == 1u);
    for (resolution = 0u; resolution <= info.params.decomposition_levels; ++resolution)
    {
        DIC_EXPECT(info.params.precinct_width_exponents[resolution] == 15u);
        DIC_EXPECT(info.params.precinct_height_exponents[resolution] == 15u);
    }
    DIC_EXPECT(info.tile_part_payload_bytes > 4u);
    DIC_EXPECT(dic_test_j2k_payload_starts_with_sop_and_has_eph(j2k_path, info.tile_part_payload_bytes));
    DIC_EXPECT(j2k_read_image_codestream(j2k_path, &decoded) == DIC_STATUS_OK);
    DIC_EXPECT(decoded.width == gray.width);
    DIC_EXPECT(decoded.height == gray.height);
    DIC_EXPECT(decoded.channels == gray.channels);
    DIC_EXPECT(memcmp(decoded.data, gray.data, dic_image_u8_sample_count(gray.width, gray.height, gray.channels)) == 0);
    dic_image_u8_free(&decoded);

    for (x = 0; x < (int)(sizeof(lossy_qualities) / sizeof(lossy_qualities[0])); ++x)
    {
        DIC_EXPECT(j2k_write_image_codestream(lossy_j2k_path, &gray, 5, lossy_qualities[x]) == DIC_STATUS_OK);
        DIC_EXPECT(j2k_read_codestream_info(lossy_j2k_path, &info) == DIC_STATUS_OK);
        DIC_EXPECT(info.params.reversible == 0u);
        DIC_EXPECT(info.params.quant_guard_bits == 7u);
        DIC_EXPECT(info.params.quant_step_count == 1u + 3u * info.params.decomposition_levels);
        DIC_EXPECT(info.params.quant_step_sizes[0] > 0.0);
        status = j2k_read_image_codestream(lossy_j2k_path, &decoded);
        if (status != DIC_STATUS_OK)
            fprintf(stderr, "lossy decode status %d for Q=%d\n", (int)status, lossy_qualities[x]);
        DIC_EXPECT(status == DIC_STATUS_OK);
        DIC_EXPECT(decoded.width == gray.width);
        DIC_EXPECT(decoded.height == gray.height);
        DIC_EXPECT(decoded.channels == gray.channels);
        if (lossy_qualities[x] == 100)
            DIC_EXPECT(dic_test_max_abs_diff(&decoded, &gray) <= 3u);
        dic_image_u8_free(&decoded);
    }
    DIC_EXPECT(j2k_write_image_codestream(lossy_j2k_path, &gray, 5, 0) == DIC_STATUS_INVALID_ARGUMENT);
    remove(lossy_j2k_path);

    DIC_EXPECT(j2k_roi_build_shift_map(gray.width, gray.height, 3, &roi_rect, &roi_shift_map) == DIC_STATUS_OK);
    DIC_EXPECT(roi_shift_map != NULL);
    DIC_EXPECT(roi_shift_map[(size_t)roi_rect.y * (size_t)gray.width + (size_t)roi_rect.x] != 0u);
    free(roi_shift_map);
    roi_shift_map = NULL;
    DIC_EXPECT(j2k_write_image_codestream_roi(roi_j2k_path, &gray, 5, &roi_rect, 4u) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_read_codestream_info(roi_j2k_path, &roi_info) == DIC_STATUS_OK);
    DIC_EXPECT(roi_info.params.roi_shift == 4u);
    DIC_EXPECT(dic_test_j2k_sod_payload_differs(
        j2k_path,
        info.tile_part_payload_bytes,
        roi_j2k_path,
        roi_info.tile_part_payload_bytes
    ));
    remove(roi_j2k_path);
    remove(j2k_path);

    DIC_EXPECT(j2k_write_image_codestream_tiled(tiled_j2k_path, &gray, 5, 4, 4, 3u) == DIC_STATUS_OK);
    DIC_EXPECT(j2k_read_codestream_info(tiled_j2k_path, &info) == DIC_STATUS_OK);
    DIC_EXPECT(info.params.width == 8u);
    DIC_EXPECT(info.params.height == 8u);
    DIC_EXPECT(info.params.tile_width == 4u);
    DIC_EXPECT(info.params.tile_height == 4u);
    DIC_EXPECT(info.params.layers == 3u);
    DIC_EXPECT(info.tile_part_count == 4u);
    DIC_EXPECT(info.total_tile_part_payload_bytes > info.tile_part_payload_bytes);
    DIC_EXPECT(j2k_read_image_codestream_layers(tiled_j2k_path, 1u, &decoded) == DIC_STATUS_OK);
    DIC_EXPECT(decoded.width == gray.width);
    DIC_EXPECT(decoded.height == gray.height);
    DIC_EXPECT(decoded.channels == gray.channels);
    dic_image_u8_free(&decoded);
    DIC_EXPECT(j2k_read_image_codestream_layers(tiled_j2k_path, 4u, &decoded) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_read_image_codestream(tiled_j2k_path, &decoded) == DIC_STATUS_OK);
    DIC_EXPECT(decoded.width == gray.width);
    DIC_EXPECT(decoded.height == gray.height);
    DIC_EXPECT(decoded.channels == gray.channels);
    DIC_EXPECT(memcmp(decoded.data, gray.data, dic_image_u8_sample_count(gray.width, gray.height, gray.channels)) == 0);
    dic_image_u8_free(&decoded);
    remove(tiled_j2k_path);

    DIC_EXPECT(dic_image_u8_alloc(&rgb, 7, 5, 3) == DIC_STATUS_OK);
    for (y = 0; y < rgb.height; ++y)
    {
        for (x = 0; x < rgb.width; ++x)
        {
            size_t pixel = ((size_t)y * (size_t)rgb.width + (size_t)x) * 3u;

            rgb.data[pixel + 0u] = (uint8_t)(x * 31 + y * 3);
            rgb.data[pixel + 1u] = (uint8_t)(x * 5 + y * 23);
            rgb.data[pixel + 2u] = (uint8_t)(x * 13 + y * 19);
        }
    }

    DIC_EXPECT(j2k_write_image_jp2(jp2_path, &rgb, 5, -1) == DIC_STATUS_OK);
    DIC_EXPECT(jp2_read_codestream_info(jp2_path, &info) == DIC_STATUS_OK);
    DIC_EXPECT(info.params.width == 7u);
    DIC_EXPECT(info.params.height == 5u);
    DIC_EXPECT(info.params.components == 3u);
    DIC_EXPECT(info.params.decomposition_levels == 3u);
    DIC_EXPECT(info.params.multiple_component_transform == 1u);
    DIC_EXPECT(info.params.use_sop == 1u);
    DIC_EXPECT(info.params.use_eph == 1u);
    DIC_EXPECT(info.params.use_precincts == 1u);
    DIC_EXPECT(info.tile_part_payload_bytes > 8u);
    DIC_EXPECT(j2k_read_image_jp2(jp2_path, &decoded) == DIC_STATUS_OK);
    DIC_EXPECT(decoded.width == rgb.width);
    DIC_EXPECT(decoded.height == rgb.height);
    DIC_EXPECT(decoded.channels == rgb.channels);
    DIC_EXPECT(memcmp(decoded.data, rgb.data, dic_image_u8_sample_count(rgb.width, rgb.height, rgb.channels)) == 0);
    dic_image_u8_free(&decoded);
    remove(jp2_path);

    DIC_EXPECT(j2k_write_image_jp2(jp2_path, &rgb, 5, 100) == DIC_STATUS_OK);
    DIC_EXPECT(jp2_read_codestream_info(jp2_path, &info) == DIC_STATUS_OK);
    DIC_EXPECT(info.params.reversible == 0u);
    DIC_EXPECT(info.params.quant_guard_bits == 7u);
    DIC_EXPECT(info.params.multiple_component_transform == 1u);
    DIC_EXPECT(j2k_read_image_jp2(jp2_path, &decoded) == DIC_STATUS_OK);
    DIC_EXPECT(decoded.width == rgb.width);
    DIC_EXPECT(decoded.height == rgb.height);
    DIC_EXPECT(decoded.channels == rgb.channels);
    DIC_EXPECT(dic_test_max_abs_diff(&decoded, &rgb) <= 4u);
    dic_image_u8_free(&decoded);
    remove(jp2_path);

    DIC_EXPECT(j2k_write_image_jp2_tiled(tiled_jp2_path, &rgb, 5, 4, 3, 2u) == DIC_STATUS_OK);
    DIC_EXPECT(jp2_read_codestream_info(tiled_jp2_path, &info) == DIC_STATUS_OK);
    DIC_EXPECT(info.params.width == 7u);
    DIC_EXPECT(info.params.height == 5u);
    DIC_EXPECT(info.params.tile_width == 4u);
    DIC_EXPECT(info.params.tile_height == 3u);
    DIC_EXPECT(info.params.layers == 2u);
    DIC_EXPECT(info.tile_part_count == 4u);
    DIC_EXPECT(j2k_read_image_jp2_layers(tiled_jp2_path, 1u, &decoded) == DIC_STATUS_OK);
    DIC_EXPECT(decoded.width == rgb.width);
    DIC_EXPECT(decoded.height == rgb.height);
    DIC_EXPECT(decoded.channels == rgb.channels);
    dic_image_u8_free(&decoded);
    DIC_EXPECT(j2k_read_image_jp2_layers(tiled_jp2_path, 3u, &decoded) == DIC_STATUS_INVALID_ARGUMENT);
    DIC_EXPECT(j2k_read_image_jp2(tiled_jp2_path, &decoded) == DIC_STATUS_OK);
    DIC_EXPECT(decoded.width == rgb.width);
    DIC_EXPECT(decoded.height == rgb.height);
    DIC_EXPECT(decoded.channels == rgb.channels);
    DIC_EXPECT(memcmp(decoded.data, rgb.data, dic_image_u8_sample_count(rgb.width, rgb.height, rgb.channels)) == 0);
    dic_image_u8_free(&decoded);
    remove(tiled_jp2_path);

    dic_image_u8_free(&gray);
    dic_image_u8_free(&rgb);
    dic_image_u8_free(&decoded);
    return 0;
}

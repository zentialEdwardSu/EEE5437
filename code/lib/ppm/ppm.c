#include "image_u8/image_u8.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs/fs.h"

enum
{
    DIC_PPM_MAX_TOKEN = 64,
    DIC_PPM_MAX_SAMPLE = 255
};

static int ppm_read_token(FILE *file, char *token, size_t token_capacity)
{
    int ch;
    size_t length = 0u;

    if (file == NULL || token == NULL || token_capacity == 0u)
        return 0;

    do
    {
        ch = fgetc(file);
        if (ch == '#')
        {
            do
            {
                ch = fgetc(file);
            } while (ch != EOF && ch != '\n' && ch != '\r');
        }
    } while (ch != EOF && isspace((unsigned char)ch));

    if (ch == EOF)
        return 0;

    do
    {
        if (length + 1u >= token_capacity)
            return 0;

        token[length] = (char)ch;
        ++length;
        ch = fgetc(file);
    } while (ch != EOF && !isspace((unsigned char)ch));

    token[length] = '\0';
    return 1;
}

static int parse_positive_int(const char *token, int *value)
{
    char *end = NULL;
    long parsed;

    if (token == NULL || value == NULL)
        return 0;

    parsed = strtol(token, &end, 10);
    if (end == token || *end != '\0' || parsed <= 0 || parsed > 1000000L)
        return 0;

    *value = (int)parsed;
    return 1;
}

dic_status dic_ppm_read(const char *path, dic_image_u8 *image)
{
    FILE *file = NULL;
    char token[DIC_PPM_MAX_TOKEN];
    int width;
    int height;
    int max_value;
    int channels;
    size_t sample_count;
    dic_status status;

    if (path == NULL || image == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;

    file = fs_open_file(path, "rb");
    if (file == NULL)
        return DIC_STATUS_FILE_OPEN_ERROR;

    if (!ppm_read_token(file, token, sizeof(token)))
    {
        fclose(file);
        return DIC_STATUS_FILE_READ_ERROR;
    }

    if (strcmp(token, "P5") == 0)
        channels = 1;
    else if (strcmp(token, "P6") == 0)
        channels = 3;
    else
    {
        fclose(file);
        return DIC_PPM_FORMAT_ERROR;
    }

    if (!ppm_read_token(file, token, sizeof(token))
        || !parse_positive_int(token, &width)
        || !ppm_read_token(file, token, sizeof(token))
        || !parse_positive_int(token, &height)
        || !ppm_read_token(file, token, sizeof(token))
        || !parse_positive_int(token, &max_value))
    {
        fclose(file);
        return DIC_PPM_FORMAT_ERROR;
    }

    if (max_value != DIC_PPM_MAX_SAMPLE)
    {
        fclose(file);
        return DIC_PPM_FORMAT_ERROR;
    }

    status = dic_image_u8_alloc(image, width, height, channels);
    if (status != DIC_STATUS_OK)
    {
        fclose(file);
        return status;
    }

    sample_count = dic_image_u8_sample_count(width, height, channels);
    if (fread(image->data, 1u, sample_count, file) != sample_count)
    {
        dic_image_u8_free(image);
        fclose(file);
        return DIC_STATUS_FILE_READ_ERROR;
    }

    fclose(file);
    return DIC_STATUS_OK;
}

dic_status dic_ppm_write(const char *path, const dic_image_u8 *image)
{
    FILE *file = NULL;
    size_t sample_count;

    if (path == NULL || image == NULL || image->data == NULL)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (image->width <= 0 || image->height <= 0)
        return DIC_STATUS_INVALID_ARGUMENT;
    if (image->channels != 1 && image->channels != 3)
        return DIC_PPM_FORMAT_ERROR;

    file = fs_open_file(path, "wb");
    if (file == NULL)
        return DIC_STATUS_IO_ERROR;

    if (fprintf(
            file,
            image->channels == 1 ? "P5\n%d %d\n255\n" : "P6\n%d %d\n255\n",
            image->width,
            image->height) < 0)
    {
        fclose(file);
        return DIC_STATUS_IO_ERROR;
    }

    sample_count = dic_image_u8_sample_count(image->width, image->height, image->channels);
    if (fwrite(image->data, 1u, sample_count, file) != sample_count)
    {
        fclose(file);
        return DIC_STATUS_IO_ERROR;
    }

    if (fclose(file) != 0)
        return DIC_STATUS_IO_ERROR;
    return DIC_STATUS_OK;
}

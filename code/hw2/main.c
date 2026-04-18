#include "errors/errors.h"
#include "hw2/hw2_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *dic_hw2_usage_text(void)
{
    return "usage: hw2 <text-file> [huffman|binary-arithmetic]\n";
}

static const char *dic_hw2_error_unknown_backend_format(void)
{
    return "error: unknown backend '%s'\n";
}

static dic_hw2_codec_backend_fn dic_hw2_pick_backend(const char *name)
{
    if (name == NULL || strcmp(name, "huffman") == 0)
        return dic_hw2_huffman_codec_backend;
    if (strcmp(name, "binary-arithmetic") == 0 || strcmp(name, "arith") == 0)
        return dic_hw2_binary_arithmetic_codec_backend;
    return NULL;
}

int main(int argc, char *argv[])
{
    dic_hw2_codec_report report;
    dic_status status;
    char *text_report = NULL;
    dic_hw2_codec_backend_fn backend = NULL;

    if (argc != 2 && argc != 3)
    {
        fputs(dic_hw2_usage_text(), stderr);
        return 1;
    }

    backend = dic_hw2_pick_backend(argc == 3 ? argv[2] : "huffman");
    if (backend == NULL)
    {
        fprintf(stderr, dic_hw2_error_unknown_backend_format(), argv[2]);
        return 1;
    }

    status = dic_hw2_codec_run_file(argv[1], backend, &report);
    if (status != DIC_STATUS_OK)
    {
        fprintf(stderr, "error: %s\n", dic_status_message(status));
        return 1;
    }

    text_report = dic_hw2_codec_build_report(argv[1], &report);
    if (text_report == NULL)
    {
        fprintf(stderr, "error: %s\n", dic_status_message(DIC_STATUS_MEMORY_ERROR));
        return 1;
    }

    printf("%s", text_report);
    free(text_report);
    return 0;
}

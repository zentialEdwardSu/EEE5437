#include "hw1/hw1_text_prob_analyzer.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reset every field so a new analysis always starts from a known clean state. */
static void dic_hw1_reset_analysis(dic_hw1_text_analysis* analysis) {
    memset(analysis, 0, sizeof(*analysis));
}

/*
 * Open the input file using a small portability wrapper.
 * MSVC prefers fopen_s(), while other compilers can use standard fopen().
 * The file is always opened in binary mode so the byte stream is preserved
 * exactly and not altered by platform-specific newline translation.
 */
static FILE* dic_hw1_open_input_file(const char* path) {
    FILE* input = NULL;

#if defined(_MSC_VER)
    if (fopen_s(&input, path, "rb") != 0) {
        return NULL;
    }
    return input;
#else
    return fopen(path, "rb");
#endif
}

/*
 * Add one block of bytes into the running frequency table.
 * Each byte value can be used directly as an index into counts[], making the
 * update loop simple and efficient.
 */
static void dic_hw1_accumulate(
    dic_hw1_text_analysis* analysis,
    const unsigned char* text,
    size_t text_size
) {
    size_t index = 0;
    for (index = 0; index < text_size; ++index) {
        ++analysis->counts[text[index]];
    }

    analysis->total_symbols += text_size;
}

/*
 * Derive summary fields from the raw occurrence counters.
 * unique_symbols counts how many different byte values were observed.
 * entropy is computed with the Shannon formula:
 *   H = -sum(p(x) * log2(p(x)))
 * For empty input, both derived values remain zero.
 */
static void dic_hw1_finalize(dic_hw1_text_analysis* analysis) {
    size_t symbol = 0;
    analysis->unique_symbols = 0;
    analysis->entropy = 0.0;

    if (analysis->total_symbols == 0) {
        return;
    }

    for (symbol = 0; symbol < DIC_HW1_SYMBOL_COUNT; ++symbol) {
        const size_t count = analysis->counts[symbol];
        double probability = 0.0;

        if (count == 0) {
            continue;
        }

        ++analysis->unique_symbols;
        probability = (double)count / (double)analysis->total_symbols;
        analysis->entropy -= probability * (log(probability) / log(2.0));
    }
}

/*
 * Produce a readable label for the report output.
 * Common control characters are shown with escape-style names, printable
 * ASCII bytes are rendered as the character itself, and everything else is
 * tagged as non-printable while the hexadecimal value is still shown by the
 * caller.
 */
static void dic_hw1_format_symbol_label(unsigned char symbol, char* label, size_t label_size) {
    switch (symbol) {
    case '\0':
        snprintf(label, label_size, "(\\0)");
        return;
    case '\a':
        snprintf(label, label_size, "(\\a)");
        return;
    case '\b':
        snprintf(label, label_size, "(\\b)");
        return;
    case '\f':
        snprintf(label, label_size, "(\\f)");
        return;
    case '\n':
        snprintf(label, label_size, "(\\n)");
        return;
    case '\r':
        snprintf(label, label_size, "(\\r)");
        return;
    case '\t':
        snprintf(label, label_size, "(\\t)");
        return;
    case '\v':
        snprintf(label, label_size, "(\\v)");
        return;
    case '\\':
        snprintf(label, label_size, "(backslash)");
        return;
    case '\'':
        snprintf(label, label_size, "(apostrophe)");
        return;
    default:
        if (symbol >= 32 && symbol <= 126) {
            snprintf(label, label_size, "('%c')", symbol);
            return;
        }
        snprintf(label, label_size, "(non-printable)");
        return;
    }
}

/*
 * Append formatted text to the report buffer while tracking the current size.
 * length is maintained by the caller so we never need to rescan the string.
 * A return value of 0 means formatting failed or the buffer would overflow,
 * allowing the caller to abort report generation cleanly.
 */
static int dic_hw1_append(
    char* buffer,
    size_t capacity,
    size_t* length,
    const char* format,
    ...
) {
    int written = 0;
    va_list arguments;

    if (*length >= capacity) {
        return 0;
    }

    va_start(arguments, format);
    written = vsnprintf(buffer + *length, capacity - *length, format, arguments);
    va_end(arguments);

    if (written < 0) {
        return 0;
    }

    if ((size_t)written >= capacity - *length) {
        return 0;
    }

    *length += (size_t)written;
    return 1;
}

/*
 * Analyze a memory buffer directly.
 * This is the lowest-level public entry point and is convenient for unit
 * tests or for data that has already been loaded by the caller.
 */
dic_hw1_status dic_hw1_analyze_text(
    const unsigned char* text,
    size_t text_size,
    dic_hw1_text_analysis* analysis
) {
    if (analysis == NULL) {
        return DIC_HW1_INVALID_ARGUMENT;
    }

    if (text_size > 0 && text == NULL) {
        return DIC_HW1_INVALID_ARGUMENT;
    }

    dic_hw1_reset_analysis(analysis);
    dic_hw1_accumulate(analysis, text, text_size);
    dic_hw1_finalize(analysis);
    return DIC_HW1_OK;
}

/*
 * Analyze file contents incrementally.
 * The function repeatedly reads fixed-size chunks with fread(), which avoids
 * loading the entire file into memory. After the loop, ferror() is checked so
 * we can distinguish a normal EOF from an actual I/O failure.
 */
dic_hw1_status dic_hw1_analyze_file(const char* path, dic_hw1_text_analysis* analysis) {
    FILE* input = NULL;
    unsigned char chunk[4096];
    size_t bytes_read = 0;

    if (path == NULL || analysis == NULL) {
        return DIC_HW1_INVALID_ARGUMENT;
    }

    input = dic_hw1_open_input_file(path);
    if (input == NULL) {
        return DIC_HW1_FILE_OPEN_ERROR;
    }

    dic_hw1_reset_analysis(analysis);

    do {
        bytes_read = fread(chunk, 1, sizeof(chunk), input);
        dic_hw1_accumulate(analysis, chunk, bytes_read);
    } while (bytes_read > 0);

    if (ferror(input) != 0) {
        fclose(input);
        return DIC_HW1_FILE_READ_ERROR;
    }

    fclose(input);
    dic_hw1_finalize(analysis);
    return DIC_HW1_OK;
}

/*
 * Convert the computed statistics into a textual report.
 * The report includes the total symbol count, number of distinct symbols,
 * entropy, and one line per observed byte with its count and probability.
 * Only symbols that actually appear are emitted to keep the output compact.
 */
char* dic_hw1_build_report(const dic_hw1_text_analysis* analysis) {
    char* report = NULL;
    size_t capacity = 0;
    size_t length = 0;
    size_t symbol = 0;

    if (analysis == NULL) {
        return NULL;
    }

    /* Estimate a large enough buffer up front to avoid dynamic growth logic. */
    capacity = 128 + (analysis->unique_symbols * 112);
    report = (char*)malloc(capacity);
    if (report == NULL) {
        return NULL;
    }

    report[0] = '\0';

    if (!dic_hw1_append(
            report,
            capacity,
            &length,
            "total_symbols=%zu\nunique_symbols=%zu\nentropy=%.6f bits/symbol\ndistribution:",
            analysis->total_symbols,
            analysis->unique_symbols,
            analysis->entropy
        )) {
        free(report);
        return NULL;
    }

    if (analysis->total_symbols == 0) {
        if (!dic_hw1_append(report, capacity, &length, "\n<empty>")) {
            free(report);
            return NULL;
        }
        /* There are no per-symbol lines to print for an empty input. */
        return report;
    }

    for (symbol = 0; symbol < DIC_HW1_SYMBOL_COUNT; ++symbol) {
        const size_t count = analysis->counts[symbol];
        double probability = 0.0;
        char label[32];

        if (count == 0) {
            continue;
        }

        probability = (double)count / (double)analysis->total_symbols;
        dic_hw1_format_symbol_label((unsigned char)symbol, label, sizeof(label));

        if (!dic_hw1_append(
                report,
                capacity,
                &length,
                "\n0x%02X %s: count=%zu, probability=%.6f",
                (unsigned int)symbol,
                label,
                count,
                probability
            )) {
            free(report);
            return NULL;
        }
    }

    return report;
}

/* Matching free function for report buffers returned by malloc(). */
void dic_hw1_free_report(char* report) {
    free(report);
}

/* Provide stable text for each status code for CLI output and debugging. */
const char* dic_hw1_status_message(dic_hw1_status status) {
    switch (status) {
    case DIC_HW1_OK:
        return "ok";
    case DIC_HW1_INVALID_ARGUMENT:
        return "invalid argument";
    case DIC_HW1_FILE_OPEN_ERROR:
        return "failed to open input file";
    case DIC_HW1_FILE_READ_ERROR:
        return "failed to read input file";
    case DIC_HW1_MEMORY_ERROR:
        return "failed to allocate memory for report";
    default:
        return "unknown error";
    }
}

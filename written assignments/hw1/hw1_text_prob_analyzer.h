#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The analyzer works on raw bytes rather than language-level characters.
 * Every possible 1-byte symbol value in the range [0, 255] is tracked,
 * which makes the implementation suitable for plain text as well as
 * arbitrary binary-like input.
 */
enum { DIC_HW1_SYMBOL_COUNT = 256 };

/*
 * Unified status codes used by the public API.
 * - DIC_HW1_OK: the requested operation completed successfully.
 * - DIC_HW1_INVALID_ARGUMENT: the caller passed a NULL pointer or another
 *   invalid argument combination.
 * - DIC_HW1_FILE_OPEN_ERROR / DIC_HW1_FILE_READ_ERROR: file I/O failed while
 *   opening or reading the input.
 * - DIC_HW1_MEMORY_ERROR: reserved for allocation failures in higher-level
 *   workflows such as report generation.
 */
typedef enum dic_hw1_status {
    DIC_HW1_OK = 0,
    DIC_HW1_INVALID_ARGUMENT = 1,
    DIC_HW1_FILE_OPEN_ERROR = 2,
    DIC_HW1_FILE_READ_ERROR = 3,
    DIC_HW1_MEMORY_ERROR = 4
} dic_hw1_status;

/*
 * Result of a completed byte-frequency analysis.
 * - counts[i] stores how many times byte value i appeared in the input.
 * - total_symbols is the total number of processed bytes.
 * - unique_symbols is the number of distinct byte values that appeared at
 *   least once.
 * - entropy stores the Shannon entropy in bits per symbol, derived from the
 *   observed probability distribution.
 */
typedef struct dic_hw1_text_analysis {
    size_t counts[DIC_HW1_SYMBOL_COUNT];
    size_t total_symbols;
    size_t unique_symbols;
    double entropy;
} dic_hw1_text_analysis;

/*
 * Analyze a caller-provided memory buffer.
 * On success, the output structure is fully overwritten with fresh statistics
 * for the given buffer contents.
 */
dic_hw1_status dic_hw1_analyze_text(
    const unsigned char* text,
    size_t text_size,
    dic_hw1_text_analysis* analysis
);

/*
 * Read a file in binary mode and analyze its byte distribution.
 * The implementation processes the file in chunks, so it scales well to
 * larger inputs without loading the whole file into memory at once.
 */
dic_hw1_status dic_hw1_analyze_file(
    const char* path,
    dic_hw1_text_analysis* analysis
);

/*
 * Build a human-readable textual report from the analysis result.
 * The returned string is heap-allocated; the caller owns it and must release
 * it with dic_hw1_free_report().
 */
char* dic_hw1_build_report(const dic_hw1_text_analysis* analysis);

/* Release a report buffer previously allocated by dic_hw1_build_report(). */
void dic_hw1_free_report(char* report);

/* Convert a status code into a stable, human-readable message string. */
const char* dic_hw1_status_message(dic_hw1_status status);

#ifdef __cplusplus
}
#endif

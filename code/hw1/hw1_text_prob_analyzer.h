#pragma once

#include "errors/errors.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Number of distinct one-byte symbols tracked by the analyzer.
 *
 * The analyzer operates on raw bytes rather than language-level characters.
 * Every possible 1-byte value in the range [0, 255] is counted, which makes
 * the implementation suitable for plain text as well as arbitrary byte data.
 */
enum { DIC_HW1_SYMBOL_COUNT = 256 };

/**
 * @brief Result of a completed byte-frequency analysis.
 *
 * The structure stores both raw occurrence counters and derived statistics.
 */
typedef struct dic_hw1_text_analysis {
    /** Occurrence count for each byte value; index i corresponds to symbol i. */
    size_t counts[DIC_HW1_SYMBOL_COUNT];
    /** Total number of processed bytes in the input. */
    size_t total_symbols;
    /** Number of distinct byte values that appeared at least once. */
    size_t unique_symbols;
    /** Shannon entropy of the observed distribution, in bits per symbol. */
    double entropy;
} dic_hw1_text_analysis;

/**
 * @brief Analyze a caller-provided memory buffer.
 *
 * On success, the output structure is fully overwritten with fresh statistics
 * for the specified buffer contents.
 *
 * @param text Pointer to the input byte buffer. May be NULL only when
 *        @p text_size is 0.
 * @param text_size Number of bytes in @p text.
 * @param analysis Output structure that receives the analysis result.
 * @return A status code indicating success or the reason for failure.
 */
dic_status dic_hw1_analyze_text(
    const unsigned char* text,
    size_t text_size,
    dic_hw1_text_analysis* analysis
);

/**
 * @brief Read a file in binary mode and analyze its byte distribution.
 *
 * The implementation processes the file incrementally in fixed-size chunks,
 * so it can handle larger inputs without loading the entire file at once.
 *
 * @param path Path to the input file.
 * @param analysis Output structure that receives the analysis result.
 * @return A status code indicating success or the reason for failure.
 */
dic_status dic_hw1_analyze_file(
    const char* path,
    dic_hw1_text_analysis* analysis
);

/**
 * @brief Build a human-readable textual report from an analysis result.
 *
 * The returned string is heap-allocated. The caller owns the returned buffer
 * and must release it with dic_hw1_free_report().
 *
 * @param analysis Input analysis result to format.
 * @return Newly allocated report string, or NULL on failure.
 */
char* dic_hw1_build_report(const dic_hw1_text_analysis* analysis);

/**
 * @brief Release a report buffer created by dic_hw1_build_report().
 *
 * @param report Report buffer to free. Passing NULL is allowed.
 */
void dic_hw1_free_report(char* report);

#ifdef __cplusplus
}
#endif

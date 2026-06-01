#pragma once

/**
 * @file finalproj_cli.h
 * @brief Argtable-based command-line interface for the final project tool.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parses command-line arguments, dispatches the selected command, and returns
 * a process exit code.
 */
int finalproj_cli_run(int argc, char **argv);

#ifdef __cplusplus
}
#endif

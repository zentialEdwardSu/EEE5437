#pragma once

/**
 * @file finalproj_cli.h
 * @brief Argtable-based command-line interface for the final project tool.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parses arguments and dispatches one final-project subcommand.
 * @param argc Argument count including the executable name.
 * @param argv Argument vector.
 * @return Process exit code: zero for success and nonzero for failure.
 */
int finalproj_cli_run(int argc, char **argv);

#ifdef __cplusplus
}
#endif

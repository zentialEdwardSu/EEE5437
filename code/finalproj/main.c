/**
 * @file main.c
 * @brief Command-line entrypoint for the final project executable.
 */

#include "finalproj/finalproj_cli.h"

/**
 * @brief Delegates all command parsing and execution to the CLI module.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status from finalproj_cli_run().
 */
int main(int argc, char **argv)
{
    return finalproj_cli_run(argc, argv);
}

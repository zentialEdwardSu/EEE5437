/**
 * @file main.c
 * @brief Command-line entrypoint for the final project executable.
 */

#include "finalproj/finalproj_cli.h"

/**
 * Delegates all command parsing and execution to the final project CLI module.
 */
int main(int argc, char **argv)
{
    return finalproj_cli_run(argc, argv);
}

#include "fs.h"

/**
 * Wrapper for fopen that handles differences between MSVC and other platforms; this is used by the project code to read and write files, and is tested by the project test suite to ensure that file I/O works correctly on all platforms.
 */
static FILE *fs_open_file(const char *path, const char *mode)
{
    FILE *file = NULL;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, mode) != 0)
        return NULL;
    return file;
#else
    return fopen(path, mode);
#endif
}
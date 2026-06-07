#include "fs.h"
#include "errors/errors.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

/**
 * Wrapper for fopen that handles differences between MSVC and other platforms; this is used by the project code to read and write files, and is tested by the project test suite to ensure that file I/O works correctly on all platforms.
 */
FILE *fs_open_file(const char *path, const char *mode)
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

FILE* fs_open_temp_file(void) {
#if defined(_WIN32)
    FILE* file = NULL;
    if (tmpfile_s(&file) != 0) return NULL;
    return file;
#else
    return tmpfile();
#endif
}

dic_status fs_replace_file(const char* source, const char* destination) {
#if defined(_WIN32)
    if (!MoveFileExA(source, destination,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return DIC_STATUS_IO_ERROR;
#else
    if (rename(source, destination) != 0) return DIC_STATUS_IO_ERROR;
#endif
    return DIC_STATUS_OK;
}
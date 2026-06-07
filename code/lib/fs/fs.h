#pragma once
#include <stdio.h>

#include "errors/errors.h"

FILE* fs_open_file(const char* path, const char* mode);
FILE* fs_open_temp_file(void);
dic_status fs_replace_file(const char* source, const char* destination);

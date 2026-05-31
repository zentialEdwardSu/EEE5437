#pragma once

#ifdef DEBUG
#include <stdio.h>
#define j2k_DEBUG_ENTER()                                                   \
    fprintf(stderr, "[DIC-J2K] %s:%d enter %s\n", __FILE__, __LINE__, __func__)
#else
#define j2k_DEBUG_ENTER() ((void)0)
#endif

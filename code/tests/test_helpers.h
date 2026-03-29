#pragma once

#include <stdio.h>
#include <stdlib.h>

static inline void dic_test_fail_impl(const char *expr, const char *file, int line)
{
    fprintf(stderr, "check failed: %s (%s:%d)\n", expr, file, line);
    exit(1);
}

#define DIC_EXPECT(expr)                                                      \
    do                                                                        \
    {                                                                         \
        if (!(expr))                                                          \
            dic_test_fail_impl(#expr, __FILE__, __LINE__);                    \
    } while (0)

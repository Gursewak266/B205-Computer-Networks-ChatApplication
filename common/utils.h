#ifndef UTILS_H
#define UTILS_H

#include <string.h>

#define SAFE_STRNCPY(dst, src, size) \
    do { \
        strncpy((dst), (src), (size) - 1); \
        (dst)[(size) - 1] = '\0'; \
    } while (0)

#define UNUSED(x)         (void)(x)
#define ARRAY_SIZE(arr)   (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b)         ((a) < (b) ? (a) : (b))
#define MAX(a, b)         ((a) > (b) ? (a) : (b))
#define STR_EMPTY(s)      ((s) == NULL || (s)[0] == '\0')

#define STRIP_NEWLINE(s) \
    do { \
        size_t _n = strlen(s); \
        while (_n > 0 && ((s)[_n-1] == '\n' || (s)[_n-1] == '\r')) \
            (s)[--_n] = '\0'; \
    } while (0)

#endif

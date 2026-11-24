/* Header with an inline helper that calls a library function. */
#include <stdio.h>

#ifndef SAMPLE_CALLS_H
#define SAMPLE_CALLS_H

static inline int show_value(int value)
{
    printf("%d\n", value);
    return value;
}

int add_three(int value);

#endif

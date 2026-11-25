/* Touchstone shared helpers. */
#include "tst_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *tst_oom_check(void *p)
{
    if (p == NULL) {
        fprintf(stderr, "touchstone: out of memory\n");
        exit(2);
    }
    return p;
}

void *tst_xmalloc(size_t n)
{
    return tst_oom_check(malloc(n == 0 ? 1 : n));
}

void *tst_xcalloc(size_t n, size_t size)
{
    return tst_oom_check(calloc(n == 0 ? 1 : n, size == 0 ? 1 : size));
}

void *tst_xrealloc(void *p, size_t n)
{
    return tst_oom_check(realloc(p, n == 0 ? 1 : n));
}

void tst_buf_init(tst_buf_t *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void tst_buf_free(tst_buf_t *b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void tst_buf_grow(tst_buf_t *b, size_t need)
{
    size_t want = b->len + need + 1;
    if (want <= b->cap)
        return;
    if (b->cap == 0)
        b->cap = 64;
    while (b->cap < want)
        b->cap *= 2;
    b->data = (char *)tst_xrealloc(b->data, b->cap);
}

void tst_buf_push(tst_buf_t *b, const char *s, size_t n)
{
    if (b->data == NULL)
        tst_buf_grow(b, n);
    else
        tst_buf_grow(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void tst_buf_pushc(tst_buf_t *b, char c)
{
    tst_buf_push(b, &c, 1);
}

void tst_buf_pushv(tst_buf_t *b, const char *fmt, va_list ap)
{
    char tmp[1024];
    va_list copy;
    int n;
    va_copy(copy, ap);
    n = vsnprintf(tmp, sizeof(tmp), fmt, copy);
    va_end(copy);
    if (n < 0)
        return;
    if ((size_t)n < sizeof(tmp)) {
        tst_buf_push(b, tmp, (size_t)n);
        return;
    }
    {
        char *big = (char *)tst_xmalloc((size_t)n + 1);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        tst_buf_push(b, big, (size_t)n);
        free(big);
    }
}

void tst_buf_pushf(tst_buf_t *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    tst_buf_pushv(b, fmt, ap);
    va_end(ap);
}

char *tst_dup_range(const char *s, size_t n)
{
    char *out = (char *)tst_xmalloc(n + 1);
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

char *tst_dup(const char *s)
{
    return tst_dup_range(s, strlen(s));
}

int tst_lower_eq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int tst_lower_contains(const char *hay, const char *needle)
{
    size_t nlen = strlen(needle);
    size_t hlen = strlen(hay);
    size_t i;
    size_t j;
    if (nlen == 0)
        return 1;
    if (nlen > hlen)
        return 0;
    for (i = 0; i + nlen <= hlen; i++) {
        for (j = 0; j < nlen; j++) {
            if (tolower((unsigned char)hay[i + j]) !=
                tolower((unsigned char)needle[j]))
                break;
        }
        if (j == nlen)
            return 1;
    }
    return 0;
}

void tst_lower_copy(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[n] = '\0';
}

/*
 * 64-bit mixing hash. Constants are the fractional digits of pi and the
 * golden ratio, both plain mathematical constants, combined by us.
 */
unsigned long long tst_mix_hash(const char *s, size_t n)
{
    unsigned long long h = 0x243f6a8885a308d3ULL;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned long long)(unsigned char)s[i];
        h *= 0x9e3779b97f4a7c15ULL;
    }
    h ^= h >> 29;
    h *= 0x9e3779b97f4a7c15ULL;
    h ^= h >> 32;
    return h;
}

unsigned long long tst_mix_step(unsigned long long h, unsigned long long v)
{
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

int tst_is_ident_start(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

int tst_is_ident_char(int c)
{
    return tst_is_ident_start(c) || (c >= '0' && c <= '9');
}

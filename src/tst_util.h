/* Touchstone shared helpers: growable buffers, text handling, mixing hash. */
#ifndef TOUCHSTONE_UTIL_H
#define TOUCHSTONE_UTIL_H

#include <stdarg.h>
#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} tst_buf_t;

void tst_buf_init(tst_buf_t *b);
void tst_buf_free(tst_buf_t *b);
void tst_buf_push(tst_buf_t *b, const char *s, size_t n);
void tst_buf_pushc(tst_buf_t *b, char c);
void tst_buf_pushf(tst_buf_t *b, const char *fmt, ...);
void tst_buf_pushv(tst_buf_t *b, const char *fmt, va_list ap);

char *tst_dup_range(const char *s, size_t n);
char *tst_dup(const char *s);
void *tst_xmalloc(size_t n);
void *tst_xcalloc(size_t n, size_t size);
void *tst_xrealloc(void *p, size_t n);
int tst_lower_eq(const char *a, const char *b);
int tst_lower_contains(const char *hay, const char *needle);
void tst_lower_copy(char *dst, const char *src, size_t n);

unsigned long long tst_mix_hash(const char *s, size_t n);
unsigned long long tst_mix_step(unsigned long long h, unsigned long long v);

int tst_is_ident_start(int c);
int tst_is_ident_char(int c);

#endif
